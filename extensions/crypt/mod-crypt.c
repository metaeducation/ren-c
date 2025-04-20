//
//  File: %mod-crypt.c
//  Summary: "Native Functions for Cryptography"
//  Section: Extension
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012 Saphirion AG
// Copyright 2012-2025 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//
// See README.md for notes about this extension.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// A. The natives follow a pattern of extracting fields up front, so that if
//    they fail we don't have to RESCUE it to clean up an initialized
//    dhm_context.  (We could put the context in a HANDLE! with a cleanup
//    function and let the system take care of the cleanup in the event of a
//    problem, but it seems better to extract first.)
//
// B. The objects representing the crypto coefficients aren't validated as
//    only having the relevant fields.  So they can have arbitrary other
//    fields.  Should there be more checking or should it stay lax?
//
// C. When mbedTLS structures are initialized they do allocations, and all
//    code paths have to free that.  By contrast, allocations done through
//    librebol will be automatically freed on failure paths--so they only
//    need to be freed on the case of successful return.


#include "reb-config.h"

#include "mbedtls/rsa.h"

// mbedTLS has separate functions for each message digest (SHA256, MD5, etc)
// and each streaming cipher (RC4, AES...) which you would have to link to
// directly with the proper parameterization.  But it also has abstraction
// layers that can list the available methods linked in, look them up by
// name, and interface with generically.
//
#include "mbedtls/md.h"
#include "mbedtls/cipher.h"

#include "mbedtls/ecdh.h"  // Elliptic curve (Diffie-Hellman)

#include "mbedtls/dhm.h"  // Diffie-Hellman (credits Merkel, by their request)

// See file %tf_snprintf.c for why we need mbedtls_platform_set_snprintf()
//
#include "mbedtls/platform.h"

#if TO_WINDOWS
    #undef _WIN32_WINNT  // https://forum.rebol.info/t/326/4
    #define _WIN32_WINNT 0x0501  // Minimum API target: WinXP
    #define WIN32_LEAN_AND_MEAN  // trim down the Win32 headers
    #include <windows.h>
    #include <wincrypt.h>
#else
    #include <fcntl.h>
    #include <unistd.h>
#endif

#include "assert-fix.h"
#include "c-enhanced.h"

// Note: sys-zlib defines Byte
#include "sys-zlib.h"  // needed for the ADLER32 hash

#include "rebol.h"
#include "tmp-mod-crypt.h"
typedef RebolValue Value;


// !!! We probably do not need to have NO_RUNTIME_CHECKS builds use memory by
// integrating the string table translating all those negative numbers into
// specific errors.  But a RUNTIME_CHECKS build might want to.  For now, just
// define one error (it's a good place to set a breakpoint).
//
INLINE Value* rebMbedtlsError(int mbedtls_ret) {
    Value* result = rebValue("make error! -{mbedTLS error}-");  // break here
    UNUSED(mbedtls_ret);  // corrupts mbedtls_ret in release build
    return result;
}


// Most routines in mbedTLS return either `void` or an `int` code which is
// 0 on success and negative numbers on error.  This macro helps generalize
// the pattern of trying to build a result and having a cleanup (similar
// ones exist in mbedTLS itself, e.g. MBEDTLS_MPI_CHK() in %bignum.h)
//
#define IF_NOT_0(label,error,call) \
    do { \
        assert(error == nullptr); \
        int mbedtls_ret = (call);  /* don't use (call) more than once! */ \
        if (mbedtls_ret != 0) { \
            error = rebMbedtlsError(mbedtls_ret); \
            goto label; \
        } \
    } while (0)


//=//// RANDOM NUMBER GENERATION //////////////////////////////////////////=//
//
// The generation of "random enough numbers" is a deep topic in cryptography.
// mbedTLS doesn't build in a random generator and allows you to pick one that
// is "as random as you feel you need" and can take advantage of any special
// "entropy sources" you have access to (e.g. the user waving a mouse around
// while the numbers are generated).  The prototype of the generator is:
//
//     int (*f_rng)(void *p_rng, unsigned char* output, size_t len);
//
// Each function that takes a random number generator also takes a pointer
// you can tunnel through (the first parameter), if it has some non-global
// state it needs to use.
//
// mbedTLS offers %ctr_drbg.h and %ctr_drbg.c for standardized functions which
// implement a "Counter mode Deterministic Random Byte Generator":
//
// https://tls.mbed.org/kb/how-to/add-a-random-generator
//
// !!! Currently we just use the code from Saphirion, given that TLS is not
// even checking the certificates it gets.
//

// Initialized by the CRYPT extension entry point, shut down by the exit code
//
#if TO_WINDOWS
    HCRYPTPROV gCryptProv = 0;
#else
    int rng_fd = -1;
#endif

int get_random(void *p_rng, unsigned char* output, size_t output_len)
{
    assert(p_rng == nullptr);  // parameter currently not used
    UNUSED(p_rng);

  #if TO_WINDOWS
    if (CryptGenRandom(gCryptProv, output_len, output) != 0)
        return 0;  // success
  #else
    if (rng_fd != -1 && read(rng_fd, output, output_len) != -1)
        return 0;  // success
  #endif

  rebJumps ("fail -{Random number generation did not succeed}-");
}



//=//// CHECKSUM "EXTENSIBLE WITH PLUG-INS" NATIVE ////////////////////////=//
//
// Rather than pollute the namespace with functions that had every name of
// every algorithm (sha256 my-data), (md5 my-data) Rebol had a CHECKSUM
// that effectively namespaced it (checksum:method my-data 'sha256).
// This suffered from somewhat the same problem as ENCODE and DECODE in that
// parameterization was not sorted out; instead leading to a hodgepodge of
// refinements that may or may not apply to each algorithm.
//
// Additionally: the idea that there is some default CHECKSUM the language
// would endorse for all time when no :METHOD is given is suspect.  It may
// be that a transient "only good for this run" sum (which wouldn't serialize)
// could be repurposed for this use.
//


//
//  Compute_IPC: C
//
// Compute an IP checksum given some data and a length.
// Used only on BINARY values.
//
uint32_t Compute_IPC(const Byte* data, size_t size)
{
    uint_fast32_t sum = 0;  // stores the summation
    const Byte* bp = data;

    while (size > 1) {
        sum += (bp[0] << 8) | bp[1];
        bp += 2;
        size -= 2;
    }

    if (size != 0)
        sum += *bp;  // Handle the odd byte if necessary

    // Add back the carry outs from the 16 bits to the low 16 bits

    sum = (sum >> 16) + (sum & 0xffff);  // Add high-16 to low-16
    sum += (sum >> 16);  // Add carry
    return cast(uint32_t, (~sum) & 0xffff);  // 1's complement, then truncate
}


//
//  export checksum: native [
//
//  "Computes a checksum, CRC, or hash"
//
//      return: "Warning: likely to be changed to always be BLOB!"
//          [blob! integer!]  ; see note below
//      method "Method name"
//          [word!]
//      data "Input data to digest (TEXT! is interpreted as UTF-8 bytes)"
//          [blob! text!]
//      :key "Returns keyed HMAC value"
//          [blob! text!]
//  ]
//
DECLARE_NATIVE(CHECKSUM)
//
// !!! The return value of this function was initially integers, and expanded
// to be either INTEGER! or BLOB!.  Allowing integer results gives some
// potential performance benefits over a binary with the same number of bits,
// although if a binary conversion is then done then it costs more.  Also, it
// introduces the question of signedness, which was inconsistent.  Moving to
// where checksum is always a BLOB! is probably what should be done.
//
// !!! There was a :SECURE option which wasn't used for anything.
//
// !!! There was a :PART feature which was removed when %sys-core.h dependency
// was removed, for simplicity.  Generic "slice" functionality is under
// consideration so every routine doesn't need to reinvent :PART.
//
// !!! There was a :HASH option that took an integer and claimed to "return
// a hash value with given size".  But what it did was:
//
//    REBINT sum = VAL_INT32(ARG(HASH));
//    if (sum <= 1)
//        sum = 1;
//    Init_Integer(OUT, Hash_Bytes(data, len) % sum);
//
// As nothing used it, it's not clear what this was for.  Currently removed.
//
// 1. Turn the method into a string and look it up in the table that mbedTLS
//    builds in when you `#include "md.h"`.  How many entries are in this
//    table depend on the config settings (see %mbedtls-rebol-config.h)
//
// 2. See %crc24-unused.c for explanation; all internal fast hashes now
//    use zlib's crc32_z(), since it is a sunk cost.  Would be:
//
//        uint32_t crc24 = Compute_CRC24(data, size);
//        return rebValue("encode [LE + 3]", crc24);
//
// 3. The interpreter uses zlib (e.g. to unpack the embedded boot code) and
//    so its hashes are a sunk cost, whether you build with any crypt
//    extension or ont.  CRC32 is typically an unsigned 32-bit number and uses
//    the full range of values.  Yet R3-Alpha chose to export this as a signed
//    integer via CHECKSUM, presumably to generate a value that could be used
//    by Rebol2, as it only had 32-bit signed INTEGER!.
//
// 4. ADLER32 is a hash available in zlib which is a sunk cost, so it was
//    exposed by Saphirion.  That happened after 64-bit integers were added,
//    and did not convert the unsigned result of the adler calculation to a
//    signed integer.
//
// 5. !!! This was an "Internet TCP 16-bit checksum" that was initially a
//    refinement (presumably because adding table entries was a pain).  It
//    does not seem to be used?
{
    INCLUDE_PARAMS_OF_CHECKSUM;

    Value* error = nullptr;
    Value* result = nullptr;

    size_t size;
    const Byte* data = rebLockBytes(&size, "data");

    char* method_utf8 = rebSpell("uppercase to text! method");  //  [1]
    const mbedtls_md_info_t* info = mbedtls_md_info_from_string(method_utf8);
    if (info)
        goto found_tls_info;  // otherwise, we look up some internal hashes

    if (0 == strcmp(method_utf8, "CRC24")) {  // prefer CRC32 (sunk cost) [2]
        error = rebValue("make error! [",
            "-{CRC24 removed: speak up if CRC32 and ADLER32 won't suffice}-",
        "]");
    }
    if (0 == strcmp(method_utf8, "CRC32")) {  // internals need for gzip [3]
        uint32_t crc = crc32_z(0L, data, size);
        result = rebValue("encode [LE + 4]", rebI(crc));
    }
    else if (0 == strcmp(method_utf8, "ADLER32")) {  // included with zlib [4]
        uint32_t adler = z_adler32(1L, data, size);  // Note the 1L (!)
        result = rebValue("encode [LE + 4]", rebI(adler));
    }
    else if (0 == strcmp(method_utf8, "TCP")) {  // !!! not used? [5]
        int ipc = Compute_IPC(data, size);
        result = rebValue("encode [LE + 2]", rebI(ipc));
    }
    else
        error = rebValue("make error! [-{Unknown CHECKSUM method:}- method]");

    goto return_result_or_fail;

  found_tls_info: { //////////////////////////////////////////////////////////

    int hmac = rebDid("key") ? 1 : 0;  // !!! int, but seems to be a boolean?

    Byte md_size = mbedtls_md_get_size(info);
    Byte* output = rebAllocN(Byte, md_size);

    struct mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    IF_NOT_0(cleanup, error, mbedtls_md_setup(&ctx, info, hmac));

    if (hmac) {
        size_t key_size;
        const Byte* key_bytes = rebLockBytes(&key_size, "key");

        IF_NOT_0(cleanup, error,
            mbedtls_md_hmac_starts(&ctx, key_bytes, key_size)
        );
        IF_NOT_0(cleanup, error, mbedtls_md_hmac_update(&ctx, data, size));
        IF_NOT_0(cleanup, error, mbedtls_md_hmac_finish(&ctx, output));

        rebUnlockBytes(key_bytes);
    }
    else {
        IF_NOT_0(cleanup, error, mbedtls_md_starts(&ctx));
        IF_NOT_0(cleanup, error, mbedtls_md_update(&ctx, data, size));
        IF_NOT_0(cleanup, error, mbedtls_md_finish(&ctx, output));
    }

    result = rebRepossess(output, md_size);

  cleanup: //////////////////////////////////////////////////////////////////

    mbedtls_md_free(&ctx);

} return_result_or_fail: { ///////////////////////////////////////////////////

    if (error)
        return rebDelegate("fail", rebR(error));

    rebFree(method_utf8);
    rebUnlockBytes(data);

    return result;
}}


//=//// INDIVIDUAL CRYPTO NATIVES /////////////////////////////////////////=//
//
// These natives are the hodgepodge of choices that implemented "enough TLS"
// to let Rebol communicate with HTTPS sites.  The first ones originated
// from Saphirion's %host-core.c:
//
// https://github.com/zsx/r3/blob/atronix/src/os/host-core.c
//
// !!! The effort to improve these has been ongoing and gradual.  Current
// focus is on building on the shared/vetted/maintained architecture of
// mbedTLS, instead of the mix of standalone clips from the Internet and some
// custom code from Saphirion.  But eventually this should aim to make
// inclusion of each crypto a separate extension for more modularity.
//


// For turning a BLOB! into an mbedTLS multiple-precision-integer ("bignum")
// Returns an mbedTLS error code if there is a problem (use with IF_NOT_0)
//
// 1. !!! It seems that `assert(mbedtls_mpi_size(X) == size)` is not always
//    true, e.g. when the first byte is 0.
//
static int Mpi_From_Binary(mbedtls_mpi* X, const Value* binary)
{
    size_t size;
    const Byte* buf = rebLockBytes(&size, binary);

    int result = mbedtls_mpi_read_binary(X, buf, size);

    assert(mbedtls_mpi_size(X) <= size);  // equal not always true [1]

    rebUnlockBytes(buf);

    return result;
}

// Opposite direction for making a BLOB! from an MPI.  Naming convention
// suggests it's an API handle and you're responsible for releasing it.
//
static Value* rebBinaryFromMpi(const mbedtls_mpi* X)
{
    size_t size = mbedtls_mpi_size(X);

    Byte* buf = rebAllocN(Byte, size);

    int result = mbedtls_mpi_write_binary(X, buf, size);

    if (result != 0)
        rebJumps ("fail -{Fatal MPI decode error}-");  // only from bugs (?)

    return rebRepossess(buf, size);
}

#define MBEDTLS_RSA_RAW_HACK -1

// RSA encrypts in units, and so if your data is not exactly the input size it
// must be padded to round to the block size.
//
//   * Using predictable data is bad (it creates weaknesses for attack)
//
//   * Using random data is bad (it means the person doing the decrypting
//     would have no way to know if the random part had been modified, in
//     order to compromise the content of the non-padded portion).
//
// Though we allow [raw] encoding it is possible to specify other methods.  It
// could be done with an object, but try a "mini-dialect" with a BLOCK!
//
void Get_Padding_And_Hash_From_Spec(
    int *padding,
    mbedtls_md_type_t *hash,
    const Value* padding_spec
){
    *padding = rebUnboxInteger(
        "let padding-list: [",
            "raw", rebI(MBEDTLS_RSA_RAW_HACK),
            "pkcs1-v15", rebI(MBEDTLS_RSA_PKCS_V15),
            "pkcs1-v21", rebI(MBEDTLS_RSA_PKCS_V21),
        "]",
        "select padding-list first", padding_spec, "else [fail [",
            "-{First element of padding spec must be one of}- @padding-list",
        "]]"
    );

    if (1 == rebUnboxInteger("length of", padding_spec)) {
        //
        // The mbedtls_rsa_set_padding() does not check this, it will only
        // fail later in the encrypt/decrypt.
        //
        if (*padding == MBEDTLS_RSA_PKCS_V21)
            rebJumps (
                "fail -{pkcs1-v21 padding scheme needs hash to be specified}-"
            );

        *hash = MBEDTLS_MD_NONE;
        return;
    }

    *hash = cast(mbedtls_md_type_t, rebUnboxInteger(
        "let hash-list: [",
            "#md5", rebI(MBEDTLS_MD_MD5),
            "#sha1", rebI(MBEDTLS_MD_SHA1),
            "#sha224", rebI(MBEDTLS_MD_SHA224),
            "#sha256", rebI(MBEDTLS_MD_SHA256),
            "#sha384", rebI(MBEDTLS_MD_SHA384),
            "#sha512", rebI(MBEDTLS_MD_SHA512),
            "#ripemd160", rebI(MBEDTLS_MD_RIPEMD160),
        "]",
        "select hash-list second", padding_spec, "else [fail [",
            "-{Second element of padding spec must be one of}- @hash-list",
        "]]"
    ));

    rebElide("if 2 != length of", padding_spec, "[",
        "fail -{Padding spec must be pad method plus optional hash}-"
    "]");
}


//
//  export rsa-generate-keypair: native [
//
//  "Generate a public and private key for encoding at most NUM-BITS of data"
//
//      return: "RSA ~[public private]~ key objects object"
//          [~[object! object!]~]
//      num-bits "How much data this key can encrypt (less when not [raw])"
//          [integer!]
//      :padding "Pad method and hash, [raw] [pkcs1-v15 #sha512] [pkcs1-v21]"
//          [block!]
//      :insecure "Allow insecure key sizes--for teaching purposes only"
//  ]
//
DECLARE_NATIVE(RSA_GENERATE_KEYPAIR)
{
    INCLUDE_PARAMS_OF_RSA_GENERATE_KEYPAIR;

    Value* padding_spec = rebValue("padding: default [[pkcs1-v15]]");

    int padding;
    mbedtls_md_type_t hash;
    Get_Padding_And_Hash_From_Spec(&padding, &hash, padding_spec);  // validate

    intptr_t num_key_bits = rebUnboxInteger("num-bits");

    bool insecure = rebDid("insecure");
    if (not insecure and num_key_bits < 1024)
        return "fail -{RSA key must be >= 1024 bits unless :INSECURE}-";
    if (num_key_bits > MBEDTLS_MPI_MAX_BITS)
        return "fail -{RSA key bits exceeds MBEDTLS_MPI_MAX_BITS}-";

    Value* error = nullptr;
    Value* public_key = nullptr;
    Value* private_key = nullptr;

  begin_code_requiring_cleanup: { // see [C] /////////////////////////////////

    struct mbedtls_rsa_context ctx;
    mbedtls_rsa_init(&ctx);

    // Public components
    //
    mbedtls_mpi N;
    mbedtls_mpi E;
    mbedtls_mpi_init(&N);
    mbedtls_mpi_init(&E);

    // Private components
    //
    mbedtls_mpi D;
    mbedtls_mpi P;
    mbedtls_mpi Q;
    mbedtls_mpi_init(&D);
    mbedtls_mpi_init(&P);
    mbedtls_mpi_init(&Q);

    // "CRT" components: these relate to a "Chinese Remainder Theorem" measure
    // for increasing the speed of decryption with RSA.  They are optional, but
    // considered a best practice when working with larger key sizes.
    //
    // https://iacr.org/archive/ches2008/51540128/51540128.pdf
    //
    mbedtls_mpi DP;
    mbedtls_mpi DQ;
    mbedtls_mpi QP;
    mbedtls_mpi_init(&DP);
    mbedtls_mpi_init(&DQ);
    mbedtls_mpi_init(&QP);

    // We don't use the padding values during generation, but make sure they
    // validate together (e.g. not using deprecated hash with spec version).
    //
    if (padding != MBEDTLS_RSA_RAW_HACK) {
        IF_NOT_0(cleanup, error, mbedtls_rsa_set_padding(
            &ctx,
            padding,
            hash
        ));
    }

    IF_NOT_0(cleanup, error, mbedtls_rsa_gen_key(
        &ctx,
        &get_random,  // f_rng, random number generating function
        nullptr,  // tunneled parameter to f_rng (unused atm, so nullptr)
        num_key_bits,
        65537  // this is what mbedTLS %gen_key.c uses for exponent (?)
    ));

    IF_NOT_0(cleanup, error, mbedtls_rsa_export(&ctx, &N, &P, &Q, &D, &E));

    IF_NOT_0(cleanup, error, mbedtls_rsa_export_crt(&ctx, &DP, &DQ, &QP));

  generate_rsa_keypair: { ////////////////////////////////////////////////////

    // "The following incomplete parameter sets for private keys are supported"
    //
    //    (1) P, Q missing.
    //    (2) D and potentially N missing.

    Value* n = rebBinaryFromMpi(&N);
    Value* e = rebBinaryFromMpi(&E);

    public_key = rebValue("make object! [",
        "padding:", padding_spec,

        "n:", n,
        "e:", e,
    "]");

    private_key = rebValue("make object! [",
        "padding:", padding_spec,

        "n:", n,
        "e:", e,

        "d:", rebR(rebBinaryFromMpi(&D)),
        "p:", rebR(rebBinaryFromMpi(&P)),
        "q:", rebR(rebBinaryFromMpi(&Q)),

        "dp:", rebR(rebBinaryFromMpi(&DP)),
        "dq:", rebR(rebBinaryFromMpi(&DQ)),
        "qinv:", rebR(rebBinaryFromMpi(&QP)),  // many call this qinv, not QP
    "]");

    rebRelease(padding_spec);

    rebRelease(n);
    rebRelease(e);

} cleanup: { /////////////////////////////////////////////////////////////////

    mbedtls_mpi_free(&DP);
    mbedtls_mpi_free(&DQ);
    mbedtls_mpi_free(&QP);

    mbedtls_mpi_free(&D);
    mbedtls_mpi_free(&P);
    mbedtls_mpi_free(&Q);

    mbedtls_mpi_free(&N);
    mbedtls_mpi_free(&E);

    mbedtls_rsa_free(&ctx);

    if (error)
        return rebDelegate("fail", rebR(error));

    return rebDelegate("pack [", rebR(public_key), rebR(private_key), "]");
}}}


//
//  export rsa-encrypt: native [
//
//  "Encrypt a *small* amount of data using the expensive RSA algorithm"
//
//      return: "Deterministic if padding is [raw], randomly blinded otherwise"
//          [blob!]
//      data "Exactly key size if [raw], else less than key size minus overhead"
//          [blob!]
//      public-key [object!]
//  ]
//
DECLARE_NATIVE(RSA_ENCRYPT)
{
    INCLUDE_PARAMS_OF_RSA_ENCRYPT;

    Value* padding_spec = rebValue(
        "match block! public-key.padding else [",
            "fail -{RSA key objects must specify at least padding: [raw]}-",
        "]"
    );

    int padding;
    mbedtls_md_type_t hash;
    Get_Padding_And_Hash_From_Spec(&padding, &hash, padding_spec);  // validate
    rebRelease(padding_spec);

    // N and E are required
    //
    Value* n = rebValue("ensure [~null~ blob!] public-key.n");
    Value* e = rebValue("ensure [~null~ blob!] public-key.e");

    if (not n or not e)
        return "fail -{RSA requires N and E components of key object}-";

    Value* error = nullptr;
    Value* result = nullptr;

  begin_code_requiring_cleanup: { // see [C] /////////////////////////////////

    struct mbedtls_rsa_context ctx;
    mbedtls_rsa_init(&ctx);

    // Public components (always used)
    //
    mbedtls_mpi N;
    mbedtls_mpi E;
    mbedtls_mpi_init(&N);
    mbedtls_mpi_init(&E);

    // Translate BLOB! public components to mbedtls BigNums
    //
    IF_NOT_0(cleanup, error, Mpi_From_Binary(&N, n));
    IF_NOT_0(cleanup, error, Mpi_From_Binary(&E, e));

    // "To setup an RSA public key, precisely N and E must have been imported"
    // This is all you need for encrypting.
    //
    IF_NOT_0(cleanup, error, mbedtls_rsa_import(
        &ctx,
        &N,  // N, The RSA modulus
        nullptr,  // P, The first prime factor of N
        nullptr,  // Q, The second prime factor of N
        nullptr,  // D, The private exponent
        &E   // E, The public exponent
    ));

    IF_NOT_0(cleanup, error, mbedtls_rsa_complete(&ctx));

  perform_encryption: { //////////////////////////////////////////////////////

    size_t plaintext_size;
    const Byte* plaintext = rebLockBytes(&plaintext_size, "data");

    size_t key_size = mbedtls_rsa_get_len(&ctx);
    Byte* encrypted = rebAllocN(  // can rebRepossess() as BLOB!
        Byte,
        key_size
    );

    if (padding == MBEDTLS_RSA_RAW_HACK) {
        if (plaintext_size != key_size) {
            error = rebValue("make error! ["
                "-{[raw] not padded,  plaintext size must equal key size}-"
            "]");
            goto cleanup;
        }

        IF_NOT_0(cleanup, error, mbedtls_rsa_public(
            &ctx,
            plaintext,
            encrypted
        ));
    }
    else {
        IF_NOT_0(cleanup, error, mbedtls_rsa_set_padding(
            &ctx,
            padding,
            hash
        ));

        IF_NOT_0(cleanup, error, mbedtls_rsa_pkcs1_encrypt(
            &ctx,
            &get_random,
            nullptr,
            plaintext_size,
            plaintext,
            encrypted  // encrypted output len will always be equal to key_size
        ));
    }

    rebUnlockBytes(plaintext);

    result = rebRepossess(encrypted, key_size);

} cleanup: { /////////////////////////////////////////////////////////////////

    mbedtls_mpi_free(&N);
    mbedtls_mpi_free(&E);

    mbedtls_rsa_free(&ctx);

    if (error)
        return rebDelegate("fail", rebR(error));

    rebRelease(n);
    rebRelease(e);

    return result;
}}}


//
//  export rsa-decrypt: native [
//
//  "Decrypt a *small* amount of data using the RSA algorithm"
//
//      return: "Decrypted data (will never be larger than the key size)"
//          [blob!]
//      data "RSA encrypted information (must be equal to key size)"
//          [blob!]
//      private-key [object!]
//  ]
//
DECLARE_NATIVE(RSA_DECRYPT)
{
    INCLUDE_PARAMS_OF_RSA_DECRYPT;

    size_t encrypted_size;
    const Byte* encrypted = rebLockBytes(&encrypted_size, "data");

    Value* padding_spec = rebValue(
        "match block! private-key.padding else [",
            "fail -{RSA key objects need at least padding: [raw]}-"
        "]"
    );

    int padding;
    mbedtls_md_type_t hash;
    Get_Padding_And_Hash_From_Spec(&padding, &hash, padding_spec);  // validate
    rebRelease(padding_spec);

    Value* n = rebValue("match blob! private-key.n");
    Value* e = rebValue("match blob! private-key.e");

    Value* d = rebValue("match blob! private-key.d");
    Value* p = rebValue("match blob! private-key.p");
    Value* q = rebValue("match blob! private-key.q");

    // "The following incomplete parameter sets for private keys are supported"
    //
    //    (1) P, Q missing.
    //    (2) D and potentially N missing.
    //
    if (n and e and d and p and q) {
        // all fields present
    }
    else if (not p and not q) {
        if (not n or not e or not d)
            return "fail -{N, E, and D needed to decrypt if P and Q missing}-";
    }
    else if (not d and not n) {
        if (not e or not p or not q)
            return "fail -{E, P, and Q needed to decrypt if D or N missing}-";
    }
    else
        return "fail -{Missing field combination in private key not allowed}-";

    Value* dp = rebValue("match blob! private-key.dp");
    Value* dq = rebValue("match blob! private-key.dq");
    Value* qinv = rebValue("match blob! private-key.qinv");

    bool chinese_remainder_speedup;

    if (not dp and not dq and not qinv) {
        chinese_remainder_speedup = false;
    }
    else if (dp and dq and qinv) {
        chinese_remainder_speedup = true;
    }
    else
        return "fail -{All of DP, DQ, and QINV must be given, or none}-";

    Value* error = nullptr;
    Value* result = nullptr;

  begin_code_requiring_cleanup: { // see [C] /////////////////////////////////

    struct mbedtls_rsa_context ctx;
    mbedtls_rsa_init(&ctx);

    // Public components (always needed)
    //
    mbedtls_mpi N;
    mbedtls_mpi E;
    mbedtls_mpi_init(&N);
    mbedtls_mpi_init(&E);

    // Private components (only used when decrypting)
    //
    mbedtls_mpi D;
    mbedtls_mpi P;
    mbedtls_mpi Q;
    mbedtls_mpi_init(&D);
    mbedtls_mpi_init(&P);
    mbedtls_mpi_init(&Q);

    // Chinese Remainder Theorem (CRT) components: optional, speed up decrypt
    //
    mbedtls_mpi DP;
    mbedtls_mpi DQ;
    mbedtls_mpi QP;
    mbedtls_mpi_init(&DP);
    mbedtls_mpi_init(&DQ);
    mbedtls_mpi_init(&QP);

    // See remarks in RSA-ENCRYPT
    //
    IF_NOT_0(cleanup, error, mbedtls_rsa_set_padding(
        &ctx,
        MBEDTLS_RSA_PKCS_V15,
        MBEDTLS_MD_SHA256
    ));

    // Translate BLOB! public components to mbedtls BigNums
    //
    if (n)
        IF_NOT_0(cleanup, error, Mpi_From_Binary(&N, n));
    IF_NOT_0(cleanup, error, Mpi_From_Binary(&E, e));

    if (d)
        IF_NOT_0(cleanup, error, Mpi_From_Binary(&D, d));
    if (p)
        IF_NOT_0(cleanup, error, Mpi_From_Binary(&P, p));
    if (q)
        IF_NOT_0(cleanup, error, Mpi_From_Binary(&Q, q));

    IF_NOT_0(cleanup, error, mbedtls_rsa_import(
        &ctx,
        n ? &N : nullptr,  // N, The RSA modulus
        p ? &P : nullptr,  // P, The first prime factor of N
        q ? &Q : nullptr,  // Q, The second prime factor of N
        d ? &D : nullptr,  // D, The private exponent
        &E  // E, The public exponent (always required)
    ));

    if (chinese_remainder_speedup) {
        IF_NOT_0(cleanup, error, Mpi_From_Binary(&DP, dp));
        IF_NOT_0(cleanup, error, Mpi_From_Binary(&DQ, dq));
        IF_NOT_0(cleanup, error, Mpi_From_Binary(&QP, qinv));

        // !!! These can be deduced from the private key components, but that
        // has some associated cost.  It appears that mbedTLS no longer has an
        // API for importing these components (though it can export them).
        // Should we argue for an API for this?  Or just check that the
        // deduction process in mbedtls_rsa_complete() gives the same values?
        // Or drop them from our object altogether?
    }

    IF_NOT_0(cleanup, error, mbedtls_rsa_complete(&ctx));

  perform_decryption: { //////////////////////////////////////////////////////

    size_t key_size = mbedtls_rsa_get_len(&ctx);
    assert(encrypted_size == key_size);

    Byte* decrypted = rebAllocN(  // can rebRepossess() as BLOB!
        Byte,
        key_size
    );

    size_t decrypted_size;

    if (padding == MBEDTLS_RSA_RAW_HACK) {
        IF_NOT_0(cleanup, error, mbedtls_rsa_private(
            &ctx,
            &get_random,
            nullptr,
            encrypted,
            decrypted
        ));

        decrypted_size = key_size;  // always true in raw RSA
    }
    else {
        IF_NOT_0(cleanup, error, mbedtls_rsa_set_padding(
            &ctx,
            padding,
            hash
        ));

        IF_NOT_0(cleanup, error, mbedtls_rsa_pkcs1_decrypt(
            &ctx,
            &get_random,
            nullptr,
            &decrypted_size,
            encrypted,
            decrypted,
            key_size  // maximum output size
        ));
        assert(decrypted_size < key_size);
    }

    result = rebRepossess(decrypted, decrypted_size);

} cleanup: { /////////////////////////////////////////////////////////////////

    mbedtls_mpi_free(&DP);
    mbedtls_mpi_free(&DQ);
    mbedtls_mpi_free(&QP);

    mbedtls_mpi_free(&D);
    mbedtls_mpi_free(&P);
    mbedtls_mpi_free(&Q);

    mbedtls_mpi_free(&N);
    mbedtls_mpi_free(&E);

    mbedtls_rsa_free(&ctx);

    if (error)
        return rebDelegate("fail", rebR(error));

    rebRelease(dp);
    rebRelease(dq);
    rebRelease(qinv);

    rebRelease(d);
    rebRelease(p);
    rebRelease(q);

    rebRelease(n);
    rebRelease(e);

    rebUnlockBytes(encrypted);

    return result;
}}}


//
//  export dh-generate-keypair: native [
//
//  "Generate a new Diffie-Hellman private/public key pair"
//
//      return: "Diffie-Hellman object with [MODULUS PRIVATE-KEY PUBLIC-KEY]"
//          [object!]
//      modulus "Public 'p', best if https://en.wikipedia.org/wiki/Safe_prime"
//          [blob!]
//      base "Public 'g', generator, less than modulus and usually prime"
//          [blob!]
//      :insecure "Don't raise errors if base/modulus choice becomes suspect"
//  ]
//
DECLARE_NATIVE(DH_GENERATE_KEYPAIR)
//
// !!! OpenSSL includes a DH_check() routine that checks for suitability of
// the Diffie Hellman parameters.  There doesn't appear to be an equivalent in
// mbedTLS at time of writing.  It might be nice to add all the checks if
// :INSECURE is not used--or should :UNCHECKED be different?
//
//   https://github.com/openssl/openssl/blob/master/crypto/dh/dh_check.c
//
// 1. The algorithms theoretically can work with a base greater than the
//    modulus.  But mbedTLS isn't expecting that, so you can get errors on
//    some cases and not others.  We'll pay the cost of validating that you
//    are not doing it (mbedTLS does not check--and lets you get away with it
//    sometimes, but not others).
{
    INCLUDE_PARAMS_OF_DH_GENERATE_KEYPAIR;

    bool insecure = rebDid("insecure");

    Value* modulus = rebValue("modulus");
    Value* base = rebValue("base");

    Value* result = nullptr;
    Value* error = nullptr;

  begin_code_requiring_cleanup: { // see [C] /////////////////////////////////

    struct mbedtls_dhm_context ctx;
    mbedtls_dhm_init(&ctx);

    mbedtls_mpi G;  // "generator" (a.k.a. base)
    mbedtls_mpi P;  // prime modulus
    mbedtls_mpi_init(&G);
    mbedtls_mpi_init(&P);

    mbedtls_mpi X;
    mbedtls_mpi_init(&X);

    IF_NOT_0(cleanup, error, Mpi_From_Binary(&G, base));
    IF_NOT_0(cleanup, error, Mpi_From_Binary(&P, modulus));

    size_t P_size;  // goto for cleanup would warn on crossing initialization
    P_size = mbedtls_mpi_size(&P);  // can't declare here, goto jumps across

    if (mbedtls_mpi_cmp_mpi(&G, &P) >= 0) {  // pay cost to validate [1]
        error = rebValue("make error! ["
            "-{Don't use base >= modulus in Diffie-Hellman.}-",
            "-{e.g. `2 mod 7` is the same as `9 mod 7` or `16 mod 7`}-"
        "]");
        goto cleanup;
    }

    IF_NOT_0(cleanup, error, mbedtls_dhm_set_group(&ctx, &P, &G));

  generate_dh_keypair: { /////////////////////////////////////////////////////

    // 1. If you remove all the leading #{00} bytes from `P`, then the private
    //    and public keys will be guaranteed to be no larger than that (due to
    //    being `mod P`, they'll always be less).  The implementation might
    //    want to ask for the smaller size, or bigger size if more arithmetic
    //    or padding is planned later on those keys.  Use `p_size` for now.

    size_t x_size = P_size;  // [1]
    size_t gx_size = P_size;

    Byte* gx = rebAllocN(Byte, gx_size);  // gx => public key
    Byte* x = rebAllocN(Byte, x_size);  // x => private key

  try_again_even_if_poor_primes: { ///////////////////////////////////////////

    // 1. mbedTLS will notify you if it discovers the base and modulus you
    //    were using is unsafe w.r.t. this attack:
    //
    //    http://www.cl.cam.ac.uk/~rja14/Papers/psandqs.pdf
    //    http://web.nvd.nist.gov/view/vuln/detail?vulnId=CVE-2005-2643
    //
    //    It can't generically notice a-priori for large base and modulus if
    //    such properties will be exposed.  So you only get this error if it
    //    runs the randomized secret calculation and happens across a worrying
    //    result.  If you get such an error it means you should be skeptical
    //    of using those numbers...and choose something more attack-resistant.
    //
    // 2. Checking for safe primes should probably be done by default, but
    //    here's some code using a probabilistic test after failure.  It can
    //    be kept here for future consideration.  Rounds chosen to scale to
    //    get 2^-80 chance of error for 4096 bits.

    int ret = mbedtls_dhm_make_public(
        &ctx,
        x_size,  // x_size (size of private key, bigger may avoid compaction)
        gx,  // output buffer (for public key returned)
        gx_size,  // olen (only ctx.len needed, bigger may avoid compaction)
        &get_random,  // f_rng (random number generator function)
        nullptr  // p_rng (first parameter tunneled to f_rng--unused ATM)
    );

    if (ret == MBEDTLS_ERR_DHM_BAD_INPUT_DATA) {  // poor primes [1]
        if (mbedtls_mpi_cmp_int(&P, 0) == 0) {
            error = rebValue(
                "make error! -{Cannot use 0 as modulus for Diffie-Hellman}-"
            );
            goto cleanup;
        }

        if (insecure)
            goto try_again_even_if_poor_primes;  // for educational use only!

        error = rebValue(
            "make error! [",
                "-{Suspiciously poor base and modulus usage was detected.}-",
                "-{Unwise to use arbitrary primes vs. constructed ones:}-",
                "{https://www.cl.cam.ac.uk/~rja14/Papers/psandqs.pdf}",
                "-{:INSECURE can override (for educational purposes, only!)}-",
            "]"
        );
        goto cleanup;
    }
    else if (ret == MBEDTLS_ERR_DHM_MAKE_PUBLIC_FAILED) {
        if (mbedtls_mpi_cmp_int(&P, 5) < 0) {
            error = rebValue(
                "make error! -{Modulus can't be < 5 for Diffie-Hellman}-"
            );
            goto cleanup;
        }

        size_t ctx_len = mbedtls_dhm_get_len(&ctx);  // byte len, not bit len
        const int rounds = (ctx_len + 1) * 10;
        int test = mbedtls_mpi_is_prime_ext(  // test primes [2]
            &P,
            rounds,
            &get_random,
            nullptr
        );
        if (test == MBEDTLS_ERR_MPI_NOT_ACCEPTABLE) {
            error = rebValue(
                "make error! [",
                    "-{Couldn't use base and modulus to generate keys.}-",
                    "-{Probabilistic test hints modulus likely not prime?}-"
                "]"
            );
            goto cleanup;
        }

        error = rebValue(
            "make error! [",
                "-{Couldn't use base and modulus to generate keys,}-",
                "-{even though modulus does appear to be prime...}-",
            "]"
        );
        goto cleanup;
    }
    else
        IF_NOT_0(cleanup, error, ret);

} extract_private_key: { /////////////////////////////////////////////////////

    // The "make_public" routine expects to be giving back a public key as
    // bytes, so it takes that buffer for output.  But it keeps the private
    // key in the context...so we have to extract that separately.
    //
    // We actually want to expose the private key vs. keep it locked up in
    // a C structure context (we dispose the context and make new ones if
    // we need them).  So extract it into a binary.

    IF_NOT_0(cleanup, error, mbedtls_dhm_get_value(
        &ctx,
        MBEDTLS_DHM_PARAM_X,
        &X
    ));
    IF_NOT_0(cleanup, error, mbedtls_mpi_write_binary(&X, x, x_size));

    result = rebValue(
        "make object! [",
            "modulus:", modulus,
            "generator:", base,  // didn't used to need to save!
            "private-key:", rebR(rebRepossess(x, x_size)),
            "public-key:", rebR(rebRepossess(gx, gx_size)),
        "]"
    );

}} cleanup: { ////////////////////////////////////////////////////////////////

    mbedtls_mpi_free(&X);

    mbedtls_mpi_free(&G);
    mbedtls_mpi_free(&P);

    mbedtls_dhm_free(&ctx);  // should free any assigned bignum fields

    if (error)
        return rebDelegate("fail", rebR(error));

    rebRelease(base);
    rebRelease(modulus);

    return result;
}}}


//
//  export dh-compute-secret: native [
//
//  "Compute secret from a private/public key pair and the peer's public key"
//
//      return: "Negotiated shared secret (same size as public/private keys)"
//          [blob!]
//      obj "The Diffie-Hellman key object"
//          [object!]
//      peer-key "Peer's public key"
//          [blob!]
//  ]
//
DECLARE_NATIVE(DH_COMPUTE_SECRET)
//
// !!! This code used to initialize ctx.P (from "modulus"), ctx.X (from
// "private-key", and ctx.GY (from the peer's public key).  There is no clear
// way to initialize X in diffie hellman contexts, e.g. preload with "our
// secret value"... so I guess it expects you to feed it P and G.  Previously
// there was no need to set G for this operation, since we already have GY.
// However, there is no longer a way to set P without setting G via
// mbedtls_dhm_set_group().
//
// 1. !!! There is no approved way to set the X field of a DHM context.  Do it
//    in an unapproved way: https://github.com/Mbed-TLS/mbedtls/issues/5818
{
    INCLUDE_PARAMS_OF_DH_COMPUTE_SECRET;

    Value* modulus = rebValue("ensure blob! obj.modulus");
    Value* generator = rebValue("ensure blob! obj.generator");
    Value* private_key = rebValue("ensure blob! obj.private-key");

    Value* result = nullptr;
    Value* error = nullptr;

  begin_code_requiring_cleanup: { // see [C] /////////////////////////////////

    struct mbedtls_dhm_context ctx;
    mbedtls_dhm_init(&ctx);

    mbedtls_mpi G;
    mbedtls_mpi P;
    mbedtls_mpi_init(&G);
    mbedtls_mpi_init(&P);

    mbedtls_mpi X;
    mbedtls_mpi_init(&X);

    IF_NOT_0(cleanup, error, Mpi_From_Binary(&G, generator));
    IF_NOT_0(cleanup, error, Mpi_From_Binary(&P, modulus));
    rebRelease(modulus);
    rebRelease(generator);

    IF_NOT_0(cleanup, error, mbedtls_dhm_set_group(&ctx, &P, &G));

    IF_NOT_0(cleanup, error, Mpi_From_Binary(&X, private_key));
    mbedtls_mpi_copy(&ctx.MBEDTLS_PRIVATE(X), &X);  // !!! HACK [1]
    rebRelease(private_key);

  extract_public_key: { //////////////////////////////////////////////////////

    // Note: mbedtls 3 only provides a "raw" import of the public key value of
    // the peer (G^Y), so we have to redo the logic of Mpi_From_Binary here.

    Value* peer_key = rebValue("peer-key");

    size_t gy_size;
    const Byte* gy_buf = rebLockBytes(&gy_size, peer_key);

    rebRelease(peer_key);

    int retcode = mbedtls_dhm_read_public(&ctx, gy_buf, gy_size);

    rebUnlockBytes(gy_buf);
    IF_NOT_0(cleanup, error, retcode);

} compute_dh_secret: { ///////////////////////////////////////////////////////

    // 1. See remarks on DH-GENERATE-KEYPAIR for why this check is performed
    //    unless :INSECURE is used.  Note that we deliberately don't allow
    //    the cases of detectably sketchy private keys to pass by even with
    //    :INSECURE set.  Instead, a new attempt is made.  So the only way
    //    this happens is if the peer came from a less checked implementation.
    //
    //    (There is no way to "try again" with unmodified mbedTLS code with a
    //    suspect key to make a shared secret--it's not randomization, it's a
    //    calculation.  Adding :INSECURE would require changing mbedTLS itself
    //    to participate in decoding insecure keys.)
    //
    // 2. !!! The multiple precision number system affords leading zeros, and
    //    can optimize them out.  So 7 could be #{0007} or #{07}.  We could
    //    pad the secret if we wanted to, but there's no obvious reason

    size_t k_size = mbedtls_dhm_get_len(&ctx);  // same size as modulus/etc.
    Byte* k_buffer = rebAllocN(Byte, k_size);  // shared key

    size_t olen;
    int ret = mbedtls_dhm_calc_secret(
        &ctx,
        k_buffer,  // output buffer for the "shared secret" key
        k_size,  // output_size (at least ctx.len, more may avoid compaction)
        &olen,  // actual number of bytes written to `s`
        &get_random,  // f_rng random number generator
        nullptr  // p_rng parameter tunneled to f_rng (not used ATM)
    );

    if (ret == MBEDTLS_ERR_DHM_BAD_INPUT_DATA) {  // poor base and modulus [1]
        error = rebValue(
            "make error! [",
                "-{Suspiciously poor base and modulus usage was detected.}-",
                "-{Unwise to use random primes vs. constructed ones.}-",
                "-{https://www.cl.cam.ac.uk/~rja14/Papers/psandqs.pdf}-",
                "-{If keys originated from Rebol, please report this!}-",
            "]"
        );
        goto cleanup;
    }
    else
        IF_NOT_0(cleanup, error, ret);

    assert(k_size >= olen);  // could pad if we wanted to, but don't [2]

    result = rebRepossess(k_buffer, k_size);

} cleanup: { /////////////////////////////////////////////////////////////////

    mbedtls_mpi_free(&X);

    mbedtls_mpi_free(&P);
    mbedtls_mpi_free(&G);

    mbedtls_dhm_free(&ctx);

    if (error)
        return rebDelegate("fail", rebR(error));

    return result;
}}}


static void Aes_Ctx_Handle_Cleaner(void* p, size_t length)
{
    struct mbedtls_cipher_context_t *ctx
        = cast(struct mbedtls_cipher_context_t*, p);
    UNUSED(length);

    mbedtls_cipher_free(ctx);
    rebFree(ctx);
}


//
//  export aes-key: native [
//
//  "Set up context for encrypting/decrypting AES data"
//
//      return: "Stream cipher context handle"
//          [handle!]
//      key [blob!]
//      iv "Optional initialization vector"
//          [blob! blank!]
//      :decrypt "Make cipher context for decryption (default is to encrypt)"
//  ]
//
DECLARE_NATIVE(AES_KEY)
{
    INCLUDE_PARAMS_OF_AES_KEY;

    size_t key_size;
    const Byte* key_bytes = rebLockBytes(&key_size, "key");

    int key_bitlen = key_size * 8;
    if (key_bitlen != 128 and key_bitlen != 192 and key_bitlen != 256) {
        return rebDelegate("fail [",
            "-{AES bits must be [128 192 256], not}-", rebI(key_bitlen),
        "]");
    }

    const mbedtls_cipher_info_t* info = mbedtls_cipher_info_from_values(
        MBEDTLS_CIPHER_ID_AES,
        key_bitlen,
        MBEDTLS_MODE_CBC
    );

    Value* error = nullptr;

  setup_cipher: { ////////////////////////////////////////////////////////////

    // 1. Default padding is PKCS7, but TLS won't work unless you use zeros.
    //    (Shown also by the %ssl_tls.c for mbedTLS, see AES CBC ciphers.)

    struct mbedtls_cipher_context_t* ctx
        = rebAlloc(struct mbedtls_cipher_context_t);
    rebUnmanageMemory(ctx);  // needs to outlive this AES-KEY function call
    mbedtls_cipher_init(ctx);

    IF_NOT_0(cleanup, error, mbedtls_cipher_setup(ctx, info));

    IF_NOT_0(cleanup, error, mbedtls_cipher_setkey(
        ctx,
        key_bytes,
        key_bitlen,
        rebDid("decrypt") ? MBEDTLS_DECRYPT : MBEDTLS_ENCRYPT
    ));

    IF_NOT_0(cleanup, error,  // Note: PKCS7 only works with zeros [1]
        mbedtls_cipher_set_padding_mode(ctx, MBEDTLS_PADDING_NONE)
    );

  setup_initialization_vector: { /////////////////////////////////////////////

    size_t blocksize = mbedtls_cipher_get_block_size(ctx);
    if (rebUnboxLogic("blob? iv")) {
        size_t iv_size;
        const Byte* iv_bytes = rebLockBytes(&iv_size, "iv");

        if (iv_size != blocksize) {
            error = rebValue("make error! [",
                "-{Initialization vector block size not}-", rebI(blocksize),
            "]");
            goto cleanup;
        }

        IF_NOT_0(cleanup, error, mbedtls_cipher_set_iv(ctx, iv_bytes, iv_size));

        rebUnlockBytes(iv_bytes);
    }
    else
        assert(rebUnboxLogic("blank? iv"));

} cleanup: { /////////////////////////////////////////////////////////////////

    if (error) {
        mbedtls_cipher_free(ctx);
        return rebDelegate("fail", rebR(error));
    }

    rebUnlockBytes(key_bytes);

    return rebHandle(
        ctx,
        sizeof(struct mbedtls_cipher_context_t),
        &Aes_Ctx_Handle_Cleaner
    );
}}}


//
//  export aes-stream: native [
//
//  "Encrypt/decrypt data using AES algorithm"
//
//      return: "Encrypted/decrypted data (null if zero length)"
//          [~null~ blob!]
//      ctx "Stream cipher context"
//          [handle!]
//      data [blob!]
//  ]
//
DECLARE_NATIVE(AES_STREAM)
{
    INCLUDE_PARAMS_OF_AES_STREAM;

    if (rebExtractHandleCleaner("ctx") != Aes_Ctx_Handle_Cleaner)
        return "fail [-{Not a AES context HANDLE!:}- @ctx]";

    size_t ilen;
    const Byte* input = rebLockBytes(&ilen, "data");

    if (ilen == 0) {
        rebUnlockBytes(input);
        return nullptr;  // !!! Is NULL a good result for 0 data?
    }

    Value* error = nullptr;
    Value* result = nullptr;

  encrypt_or_decrypt: { //////////////////////////////////////////////////////

    // 1. !!! Saphir's AES code worked with zero-padded chunks, so you always
    //    got a multiple of 16 bytes out.  That doesn't seem optimal for a
    //    "streaming cipher" because for the output to be useful, your input
    //    has to be pre-chunked; you should be able to add one byte at a time
    //    if you want.  The code is kept compatible just to excise the old
    //    AES implementation--but this needs to change, maybe to a PORT! model
    //    of some kind.

    struct mbedtls_cipher_context_t *ctx
        = rebUnboxHandle(struct mbedtls_cipher_context_t*, "ctx");

    size_t blocksize = mbedtls_cipher_get_block_size(ctx);
    assert(blocksize == 16);  // !!! to be generalized...

    size_t pad_len = (((ilen - 1) >> 4) << 4) + blocksize;  // !!! review [1]

    if (ilen < pad_len) {  // need new input data with zero padding
        Byte* pad_data = rebAllocN(Byte, pad_len);
        memset(pad_data, 0, pad_len);
        memcpy(pad_data, input, ilen);
        input = pad_data;
    }

    Byte* output = rebAllocN(
        Byte,
        ilen + blocksize  // output data buffer must be at *least* this length
    );

    size_t olen;
    IF_NOT_0(cleanup, error,
        mbedtls_cipher_update(ctx, input, pad_len, output, &olen)
    );

    result = rebRepossess(output, olen);

} cleanup: { /////////////////////////////////////////////////////////////////

    if (error)
        return rebDelegate("fail", error);

    rebUnlockBytes(input);

    return result;
}}


// For reasons that don't seem particularly good for a generic cryptography
// library that is not entirely TLS-focused, the 25519 curve isn't in the
// main list of curves:
//
// https://github.com/ARMmbed/mbedtls/issues/464
//
struct mbedtls_ecp_curve_info curve25519_info =
    { MBEDTLS_ECP_DP_CURVE25519, 29, 256, "curve25519"};


static const struct mbedtls_ecp_curve_info* Ecp_Curve_Info_From_Name(
    const char* name
){
    if (0 == strcmp(name, "curve25519"))
        return &curve25519_info;

    const struct mbedtls_ecp_curve_info *info;
    info = mbedtls_ecp_curve_info_from_name(name);

    if (info)
        return info;

    rebJumps ("fail [-{Unknown ECC curve specified:}-", rebT(name), "]");
}


//
//  export ecc-generate-keypair: native [
//
//  "Generates an uncompressed secp256r1 key"
//
//      return: "object with PUBLIC/X, PUBLIC/Y, and PRIVATE key members"
//          [object!]
//      group "Elliptic curve group [CURVE25519 SECP256R1 ...]"  ; [1]
//          [word!]
//  ]
//
DECLARE_NATIVE(ECC_GENERATE_KEYPAIR)
//
// 1. !!! Using curve25519 seems to always give a y coordinate of zero in the
//    public key.  Is this correct (it seems to yield the right secret)?
{
    INCLUDE_PARAMS_OF_ECC_GENERATE_KEYPAIR;

    char* group = rebSpell("lowercase to text! group");
    const struct mbedtls_ecp_curve_info *info = Ecp_Curve_Info_From_Name(group);
    rebFree(group);

    size_t num_bytes = info->bit_size / 8;

    Value* error = nullptr;
    Value* result = nullptr;

  begin_code_requiring_cleanup: { // see [C] /////////////////////////////////

    // 1. A change in mbedTLS ecdh code means there's a context variable in
    //    the context (ctx.ctx) when not using MBEDTLS_ECDH_LEGACY_CONTEXT
    //
    // 2. !!! The mbedtls 3.0 transition has not established a way to get at
    //    the private fields via functions.  They cheat via MBEDTLS_PRIVATE:
    //
    //      https://github.com/Mbed-TLS/mbedtls/issues/5016

    struct mbedtls_ecdh_context ctx;
    mbedtls_ecdh_init(&ctx);  // legacy context variable ctx.ctx [2]

    IF_NOT_0(cleanup, error,
        mbedtls_ecdh_setup(&ctx, info->grp_id)
    );

    mbedtls_ecdh_context_mbed *mbed_ecdh;  // !!! no private field API [3]
    mbed_ecdh = &ctx.MBEDTLS_PRIVATE(ctx).MBEDTLS_PRIVATE(mbed_ecdh);

    IF_NOT_0(cleanup, error, mbedtls_ecdh_gen_public(
        &mbed_ecdh->MBEDTLS_PRIVATE(grp),
        &mbed_ecdh->MBEDTLS_PRIVATE(d),  // private key
        &mbed_ecdh->MBEDTLS_PRIVATE(Q),  // public key (X, Y)
        &get_random,  // f_rng, random number generator
        nullptr  // p_rng, parameter tunneled to random generator (unused atm)
    ));

  generate_ecc_keypair: { ////////////////////////////////////////////////////

    uint8_t* p_publicX = rebAllocN(uint8_t, num_bytes);
    uint8_t* p_publicY = rebAllocN(uint8_t, num_bytes);
    uint8_t* p_privateKey = rebAllocN(uint8_t, num_bytes);

    mbedtls_mpi_write_binary(
        &mbed_ecdh->MBEDTLS_PRIVATE(Q).MBEDTLS_PRIVATE(X), p_publicX, num_bytes
    );
    mbedtls_mpi_write_binary(
        &mbed_ecdh->MBEDTLS_PRIVATE(Q).MBEDTLS_PRIVATE(Y), p_publicY, num_bytes
    );
    mbedtls_mpi_write_binary(
        &mbed_ecdh->MBEDTLS_PRIVATE(d), p_privateKey, num_bytes
    );

    result = rebValue(
        "make object! [",
            "public-key: make object! [",
                "x:", rebR(rebRepossess(p_publicX, num_bytes)),
                "y:", rebR(rebRepossess(p_publicY, num_bytes)),
            "]",
            "private-key:", rebR(rebRepossess(p_privateKey, num_bytes)),
        "]"
    );

} cleanup: { /////////////////////////////////////////////////////////////////

    mbedtls_ecdh_free(&ctx);

    if (error)
        return rebDelegate("fail", rebR(error));

    return result;
}}}


//
//  export ecdh-shared-secret: native [
//      return: "secret"
//          [blob!]
//      group "Elliptic curve group [CURVE25519 SECP256R1 ...]"
//          [word!]
//      private-key "32-byte private key"
//          [blob!]
//      public-key "64-byte public key of peer (or OBJECT! with 32-byte X & Y)"
//          [blob! object!]
//  ]
//
DECLARE_NATIVE(ECDH_SHARED_SECRET)
{
    INCLUDE_PARAMS_OF_ECDH_SHARED_SECRET;

    char* group = rebSpell("lowercase to text! group");
    const struct mbedtls_ecp_curve_info *info = Ecp_Curve_Info_From_Name(group);
    rebFree(group);

    size_t num_bytes = info->bit_size / 8;

    Value* private_key = rebValue("private-key");
    size_t private_key_len = rebUnboxInteger("length of private-key");

    Byte* public_bytes = rebAllocN(Byte, num_bytes * 2);

    rebBytesInto(public_bytes, num_bytes * 2,
        "let bin: either blob? public-key [public-key] [",
            "append (copy public-key.x) public-key.y"
        "]",
        "if", rebI(num_bytes * 2), "!= length of bin [",
            "fail [-{Public BLOB! must be}-", rebI(num_bytes * 2),
                "-{bytes total for}- group]",
        "]",
        "bin"
    );

    Value* result = nullptr;
    Value* error = nullptr;

  begin_code_requiring_cleanup: { // see [C] /////////////////////////////////

    // 1. A change in mbedTLS ecdh code means there's a context variable in
    //    the context (ctx.ctx) when not using MBEDTLS_ECDH_LEGACY_CONTEXT
    //
    // 2. !!! The mbedtls 3.0 transition has not established a way to get at
    //    the private fields via functions.  They cheat via MBEDTLS_PRIVATE:
    //
    //      https://github.com/Mbed-TLS/mbedtls/issues/5016

    struct mbedtls_ecdh_context ctx;
    mbedtls_ecdh_init(&ctx);  // legacy context variable ctx.ctx [1]

    IF_NOT_0(cleanup, error,
        mbedtls_ecdh_setup(&ctx, info->grp_id)
    );

    mbedtls_ecdh_context_mbed *mbed_ecdh;  // !!! no private field API [2]
    mbed_ecdh = &ctx.MBEDTLS_PRIVATE(ctx).MBEDTLS_PRIVATE(mbed_ecdh);

    IF_NOT_0(cleanup, error, mbedtls_mpi_read_binary(
        &mbed_ecdh->MBEDTLS_PRIVATE(Qp).MBEDTLS_PRIVATE(X),
        public_bytes,
        num_bytes
    ));
    IF_NOT_0(cleanup, error, mbedtls_mpi_read_binary(
        &mbed_ecdh->MBEDTLS_PRIVATE(Qp).MBEDTLS_PRIVATE(Y),
        public_bytes + num_bytes,
        num_bytes
    ));
    IF_NOT_0(cleanup, error, mbedtls_mpi_lset(
        &mbed_ecdh->MBEDTLS_PRIVATE(Qp).MBEDTLS_PRIVATE(Z),
        1
    ));

    if (num_bytes != private_key_len) {
        error = rebValue("make error! ["
            "-{Private key must be}-", rebI(num_bytes),
            "-{bytes for}- group]",
        "]");
        goto cleanup;
    }

    IF_NOT_0(cleanup, error,
        Mpi_From_Binary(&mbed_ecdh->MBEDTLS_PRIVATE(d), private_key));

  calculate_ecdh_secret: { ///////////////////////////////////////////////////

    uint8_t *secret_bytes = rebAllocN(uint8_t, num_bytes);
    size_t olen;
    IF_NOT_0(cleanup, error, mbedtls_ecdh_calc_secret(
        &ctx,
        &olen,
        secret_bytes,
        num_bytes,
        &get_random,
        nullptr
    ));
    assert(olen == num_bytes);
    result = rebRepossess(secret_bytes, num_bytes);

} cleanup: { /////////////////////////////////////////////////////////////////

    mbedtls_ecdh_free(&ctx);

    if (error)
        return rebDelegate("fail", rebR(error));

    rebRelease(private_key);
    rebFree(public_bytes);

    return result;
}}}

EXTERN_C int tf_snprintf(char* s, size_t n, const char* fmt, ...);


//
//  startup*: native [
//
//  "Initialize random number generators and OS-provided crypto services"
//
//      return: [~]
//  ]
//
DECLARE_NATIVE(STARTUP_P)
{
    INCLUDE_PARAMS_OF_STARTUP_P;

    mbedtls_platform_set_snprintf(&tf_snprintf);  // see file %tf_snprintf.c

  #if TO_EMSCRIPTEN

    // !!! No random number generation, yet:
    // https://github.com/WebAssembly/wasi-random
    //
    return rebTrash();

  #elif TO_WINDOWS

    if (CryptAcquireContextW(
        &gCryptProv,
        0,
        0,
        PROV_RSA_FULL,
        CRYPT_VERIFYCONTEXT | CRYPT_SILENT
    )){
        return rebTrash();
    }
    gCryptProv = 0;

  #else  // assume Linux-like system

    rng_fd = open("/dev/urandom", O_RDONLY);
    if (rng_fd != -1)
        return rebTrash();

  #endif

    // !!! Should we fail here, or wait to fail until the system tries to
    // generate random data and cannot?
    //
    return "fail -{Crypto STARTUP* can't init random number generator}-";
}


//
//  shutdown*: native [
//
//  "Shut down random number generators and OS-provided crypto services"
//
//      return: [~]
//  ]
//
DECLARE_NATIVE(SHUTDOWN_P)
{
    INCLUDE_PARAMS_OF_SHUTDOWN_P;

  #if TO_WINDOWS
    if (gCryptProv != 0) {
        CryptReleaseContext(gCryptProv, 0);
        gCryptProv = 0;
    }
  #else
    if (rng_fd != -1) {
        close(rng_fd);
        rng_fd = -1;
    }
  #endif

    return rebTrash();
}
