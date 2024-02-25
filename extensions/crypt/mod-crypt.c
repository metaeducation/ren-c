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
// Copyright 2012-2023 Ren-C Open Source Contributors
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

#include "sys-zlib.h"  // needed for the ADLER32 hash

#include "tmp-mod-crypt.h"
typedef RebolValue Value;


// !!! We probably do not need to have non-debug builds use up memory by
// integrating the string table translating all those negative numbers into
// specific errors.  But a debug build might want to.  For now, one error
// (good place to set a breakpoint).
//
inline static Value* rebMbedtlsError(int mbedtls_ret) {
    Value* result = rebValue("make error! {mbedTLS error}");  // break here
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

  rebJumps ("fail {Random number generation did not succeed}");
}



//=//// CHECKSUM "EXTENSIBLE WITH PLUG-INS" NATIVE ////////////////////////=//
//
// Rather than pollute the namespace with functions that had every name of
// every algorithm (`sha256 my-data`, `md5 my-data`) Rebol had a CHECKSUM
// that effectively namespaced it (e.g. `checksum/method my-data 'sha256`).
// This suffered from somewhat the same problem as ENCODE and DECODE in that
// parameterization was not sorted out; instead leading to a hodgepodge of
// refinements that may or may not apply to each algorithm.
//
// Additionally: the idea that there is some default CHECKSUM the language
// would endorse for all time when no /METHOD is given is suspect.  It may
// be that a transient "only good for this run" sum (which wouldn't serialize)
// could be repurposed for this use.
//


//
//  Compute_IPC: C
//
// Compute an IP checksum given some data and a length.
// Used only on BINARY values.
//
uint32_t Compute_IPC(const unsigned char* data, size_t size)
{
    uint_fast32_t sum = 0;  // stores the summation
    const unsigned char* bp = data;

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
//      return: "Warning: likely to be changed to always be BINARY!"
//          [binary! integer!]  ; see note below
//      method "Method name"
//          [word!]
//      data "Input data to digest (TEXT! is interpreted as UTF-8 bytes)"
//          [binary! text!]
//      /key "Returns keyed HMAC value"
//          [binary! text!]
//  ]
//
DECLARE_NATIVE(checksum)
//
// !!! The return value of this function was initially integers, and expanded
// to be either INTEGER! or BINARY!.  Allowing integer results gives some
// potential performance benefits over a binary with the same number of bits,
// although if a binary conversion is then done then it costs more.  Also, it
// introduces the question of signedness, which was inconsistent.  Moving to
// where checksum is always a BINARY! is probably what should be done.
//
// !!! There was a /SECURE option which wasn't used for anything.
//
// !!! There was a /PART feature which was removed when %sys-core.h dependency
// was removed, for simplicity.  Generic "slice" functionality is under
// consideration so every routine doesn't need to reinvent /PART.
//
// !!! There was a /HASH option that took an integer and claimed to "return
// a hash value with given size".  But what it did was:
//
//    REBINT sum = VAL_INT32(ARG(hash));
//    if (sum <= 1)
//        sum = 1;
//    Init_Integer(OUT, Hash_Bytes(data, len) % sum);
//
// As nothing used it, it's not clear what this was for.  Currently removed.
{
    CRYPT_INCLUDE_PARAMS_OF_CHECKSUM;

    size_t size;
    unsigned char* data = rebBytes(&size, rebArgR("data"));

    // Turn the method into a string and look it up in the table that mbedTLS
    // builds in when you `#include "md.h"`.  How many entries are in this
    // table depend on the config settings (see %mbedtls-rebol-config.h)
    //
    char* method_utf8 = rebSpell("uppercase to text! @", rebArgR("method"));

    const mbedtls_md_info_t* info = mbedtls_md_info_from_string(method_utf8);
    if (info) {
        rebFree(method_utf8);
        goto found_tls_info;
    }

    // Look up some internally available methods.
    //
    if (0 == strcmp(method_utf8, "CRC24")) {
        rebFree(method_utf8);
        //
        // See %crc24-unused.c for explanation; all internal fast hashes now
        // use zlib's crc32_z(), since it is a sunk cost.
        //
        // Would be:
        //
        //   uint32_t crc24 = Compute_CRC24(data, size);
        //   return rebValue("enbin [le + 3]", crc24);
        //
        rebJumps (
            "fail {CRC24 currently disabled, speak up if you use it}"
        );
    }
    if (0 == strcmp(method_utf8, "CRC32")) {
        //
        // CRC32 is a hash needed for gzip which is a sunk cost, and it
        // was exposed in R3-Alpha.  It is typically an unsigned 32-bit
        // number and uses the full range of values.  Yet R3-Alpha chose to
        // export this as a signed integer via CHECKSUM, presumably to
        // generate a value that could be used by Rebol2, as it only had
        // 32-bit signed INTEGER!.
        //
        rebFree(method_utf8);
        uint32_t crc = crc32_z(0L, data, size);
        return rebValue("enbin [le + 4]", rebI(crc));
    }
    else if (0 == strcmp(method_utf8, "ADLER32")) {
        //
        // ADLER32 is a hash available in zlib which is a sunk cost, so
        // it was exposed by Saphirion.  That happened after 64-bit
        // integers were available, and did not convert the unsigned
        // result of the adler calculation to a signed integer.
        //
        rebFree(method_utf8);
        uint32_t adler = z_adler32(1L, data, size);  // Note the 1L (!)
        return rebValue("enbin [le + 4]", rebI(adler));
    }
    else if (0 == strcmp(method_utf8, "TCP")) {
        //
        // !!! This was an "Internet TCP 16-bit checksum" that was initially
        // a refinement (presumably because adding table entries was a pain).
        // It does not seem to be used?
        //
        rebFree(method_utf8);
        int ipc = Compute_IPC(data, size);
        return rebValue("enbin [le + 2]", rebI(ipc));
    }

    rebJumps (
        "fail [{Unknown CHECKSUM method:} @", rebArgR("method"), "]"
    );

  found_tls_info: {
    Value* key = rebArg("key");

    int hmac = key ? 1 : 0;  // !!! int, but seems to be a boolean?

    unsigned char md_size = mbedtls_md_get_size(info);
    unsigned char* output = rebAllocN(unsigned char, md_size);

    Value* error = nullptr;
    Value* result = nullptr;

    struct mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    IF_NOT_0(cleanup, error, mbedtls_md_setup(&ctx, info, hmac));

    if (hmac) {
        size_t key_size;
        unsigned char* key_bytes = rebBytes(&key_size, key);

        IF_NOT_0(cleanup, error,
            mbedtls_md_hmac_starts(&ctx, key_bytes, key_size)
        );
        IF_NOT_0(cleanup, error, mbedtls_md_hmac_update(&ctx, data, size));
        IF_NOT_0(cleanup, error, mbedtls_md_hmac_finish(&ctx, output));

        rebFree(key_bytes);
    }
    else {
        IF_NOT_0(cleanup, error, mbedtls_md_starts(&ctx));
        IF_NOT_0(cleanup, error, mbedtls_md_update(&ctx, data, size));
        IF_NOT_0(cleanup, error, mbedtls_md_finish(&ctx, output));
    }

    rebRelease(key);

    result = rebRepossess(output, md_size);

  cleanup:

    mbedtls_md_free(&ctx);

    rebFree(data);

    if (error)
        rebJumps ("fail", error);

    return result;
  }
}


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


// For turning a BINARY! into an mbedTLS multiple-precision-integer ("bignum")
// Returns an mbedTLS error code if there is a problem (use with IF_NOT_0)
//
static int Mpi_From_Binary(mbedtls_mpi* X, const Value* binary)
{
    size_t size;
    unsigned char* buf = rebBytes(&size, binary);  // allocates w/rebMalloc()

    int result = mbedtls_mpi_read_binary(X, buf, size);

    // !!! It seems that `assert(mbedtls_mpi_size(X) == size)` is not always
    // true, e.g. when the first byte is 0.
    //
    assert(mbedtls_mpi_size(X) <= size);

    rebFree(buf);  // !!! This could use a non-copying binary reader API

    return result;
}

// Opposite direction for making a BINARY! from an MPI.  Naming convention
// suggests it's an API handle and you're responsible for releasing it.
//
static Value* rebBinaryFromMpi(const mbedtls_mpi* X)
{
    size_t size = mbedtls_mpi_size(X);

    unsigned char* buf = rebAllocN(unsigned char, size);

    int result = mbedtls_mpi_write_binary(X, buf, size);

    if (result != 0)
        rebJumps ("fail {Fatal MPI decode error}");  // only from bugs (?)

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
            "{First element of padding spec must be one of} mold padding-list",
        "]]"
    );

    if (1 == rebUnboxInteger("length of", padding_spec)) {
        //
        // The mbedtls_rsa_set_padding() does not check this, it will only
        // fail later in the encrypt/decrypt.
        //
        if (*padding == MBEDTLS_RSA_PKCS_V21)
            rebJumps (
                "fail {pkcs1-v21 padding scheme needs hash to be specified}"
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
            "{Second element of padding spec must be one of} mold hash-list",
        "]]"
    ));

    if (2 != rebUnboxInteger("length of", padding_spec))
        rebJumps (
            "fail {Padding spec must be pad method plus optional hash}"
        );
}


//
//  export rsa-generate-keypair: native [
//
//  "Generate a public and private key for encoding at most NUM-BITS of data"
//
//      return: "RSA public key object"
//          [object!]
//      @private-key "RSA private key object (required output)"
//          [object!]
//      num-bits "How much data this key can encrypt (less when not [raw])"
//          [integer!]
//      /padding "Pad method and hash, [raw] [pkcs1-v15 #sha512] [pkcs1-v21]"
//          [block!]
//      /insecure "Allow insecure key sizes--for teaching purposes only"
//  ]
//
DECLARE_NATIVE(rsa_generate_keypair)
{
    CRYPT_INCLUDE_PARAMS_OF_RSA_GENERATE_KEYPAIR;

    Value* padding_spec;
    if (not rebDid(rebArgR("padding")))
        padding_spec = rebValue("[pkcs1-v15]");  // mbedtls_init() uses, no hash
    else
        padding_spec = rebValue(rebArgR("padding"));  // easier to just free it

    int padding;
    mbedtls_md_type_t hash;
    Get_Padding_And_Hash_From_Spec(&padding, &hash, padding_spec);  // validate

    intptr_t num_key_bits = rebUnboxInteger(rebArgR("num-bits"));

    bool insecure = rebDid(rebArgR("insecure"));
    if (not insecure and num_key_bits < 1024)
        rebJumps (
            "fail {RSA key must be >= 1024 bits in size unless /INSECURE}"
        );
    if (num_key_bits > MBEDTLS_MPI_MAX_BITS)
        rebJumps (
            "fail {RSA key bits exceeds MBEDTLS_MPI_MAX_BITS}"
        );

    Value* error = nullptr;
    Value* public_key = nullptr;
    Value* private_key = nullptr;

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

  blockscope {
    Value* n = rebBinaryFromMpi(&N);
    Value* e = rebBinaryFromMpi(&E);

    public_key = rebValue("make object! [",
        "padding:", padding_spec,

        "n:", n,
        "e:", e,
    "]");

    // "The following incomplete parameter sets for private keys are supported"
    //
    //    (1) P, Q missing.
    //    (2) D and potentially N missing.
    //
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
  }

  cleanup:
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
        rebJumps ("fail", error);

    // !!! Usermode multi returns faces a problem if Value* cannot hold
    // packs (which is the current philosophy).  This means you couldn't
    // say `return rebValue("pack [1 2]")` etc.  So you have to SET a WORD!
    // in the function interface... but that requires getting a bound word
    // to the argument in the frame.
    //
    // The clunky solution at present is to use rebDelegate() to return a
    // PACK! containing the values.
    //
    return rebDelegate("pack [", rebR(public_key), rebR(private_key), "]");
}


//
//  export rsa-encrypt: native [
//
//  "Encrypt a *small* amount of data using the expensive RSA algorithm"
//
//      return: "Deterministic if padding is [raw], randomly blinded otherwise"
//          [binary!]
//      data "Exactly key size if [raw], else less than key size minus overhead"
//          [binary!]
//      public-key [object!]
//  ]
//
DECLARE_NATIVE(rsa_encrypt)
{
    CRYPT_INCLUDE_PARAMS_OF_RSA_ENCRYPT;

    Value* obj = rebArg("public-key");  // type checking ensures OBJECT!

    Value* padding_spec = rebValue("match block! select", obj, "'padding");
    if (not padding_spec)
        rebJumps (
            "fail {RSA key objects must specify at least padding: [raw]}"
        );

    int padding;
    mbedtls_md_type_t hash;
    Get_Padding_And_Hash_From_Spec(&padding, &hash, padding_spec);  // validate
    rebRelease(padding_spec);

    // N and E are required
    //
    Value* n = rebValue("match binary! select", obj, "'n");
    Value* e = rebValue("match binary! select", obj, "'e");

    if (not n or not e)
        rebJumps ("fail {RSA requires N and E components of key object}");

    rebRelease(obj);

    struct mbedtls_rsa_context ctx;
    mbedtls_rsa_init(&ctx);

    // Public components (always used)
    //
    mbedtls_mpi N;
    mbedtls_mpi E;
    mbedtls_mpi_init(&N);
    mbedtls_mpi_init(&E);

    Value* error = nullptr;
    Value* result = nullptr;

    // Translate BINARY! public components to mbedtls BigNums
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

  blockscope {
    //
    // !!! This makes a copy of the data being encrypted.  The API should
    // likely offer "raw" data access under some constraints (e.g. locking
    // the data from relocation or resize).
    //
    size_t plaintext_size;
    unsigned char* plaintext = rebBytes(&plaintext_size, rebArgR("data"));

    // Buffer suitable for recapturing as a BINARY!
    //
    size_t key_size = mbedtls_rsa_get_len(&ctx);
    unsigned char* encrypted = rebAllocN(unsigned char, key_size);

    if (padding == MBEDTLS_RSA_RAW_HACK) {
        if (plaintext_size != key_size) {
            error = rebValue(
                "[raw] isn't padded, requires plaintext size to equal key size"
            );
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

    rebFree(plaintext);

    result = rebRepossess(encrypted, key_size);
  }

  cleanup:
    mbedtls_mpi_free(&N);
    mbedtls_mpi_free(&E);

    rebRelease(n);
    rebRelease(e);

    mbedtls_rsa_free(&ctx);

    if (error)
        rebJumps ("fail", error);

    return result;
}


//
//  export rsa-decrypt: native [
//
//  "Decrypt a *small* amount of data using the RSA algorithm"
//
//      return: "Decrypted data (will never be larger than the key size)"
//          [binary!]
//      data "RSA encrypted information (must be equal to key size)"
//          [binary!]
//      private-key [object!]
//  ]
//
DECLARE_NATIVE(rsa_decrypt)
{
    CRYPT_INCLUDE_PARAMS_OF_RSA_DECRYPT;

    Value* obj = rebArg("private_key");  // type checking ensures OBJECT!

  //=//// EXTRACT INPUT PARAMETERS /////////////////////////////////////////=//

    Value* padding_spec = rebValue("match block! select", obj, "'padding");
    if (not padding_spec)
        rebJumps ("fail {RSA key objects need at least padding: [raw]}");

    int padding;
    mbedtls_md_type_t hash;
    Get_Padding_And_Hash_From_Spec(&padding, &hash, padding_spec);  // validate
    rebRelease(padding_spec);

    Value* n = rebValue("match binary! select", obj, "'n");
    Value* e = rebValue("match binary! select", obj, "'e");

    Value* d = rebValue("match binary! select", obj, "'d");
    Value* p = rebValue("match binary! select", obj, "'p");
    Value* q = rebValue("match binary! select", obj, "'q");

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
            rebJumps (
                "fail {N, E, and D needed to decrypt if P and Q are missing}"
            );
    }
    else if (not d and not n) {
        if (not e or not p or not q)
            rebJumps (
                "fail {E, P, and Q needed to decrypt if D or N are missing}"
            );
    }
    else
        rebJumps (
            "fail {Missing field combination in private key not allowed}"
        );

    Value* dp = rebValue("match binary! select", obj, "'dp");
    Value* dq = rebValue("match binary! select", obj, "'dq");
    Value* qinv = rebValue("match binary! select", obj, "'qinv");

    bool chinese_remainder_speedup;

    if (not dp and not dq and not qinv) {
        chinese_remainder_speedup = false;
    }
    else if (dp and dq and qinv) {
        chinese_remainder_speedup = true;
    }
    else
        rebJumps (
            "fail {All of DP, DQ, and QINV fields must be given, or none}"
        );

    rebRelease(obj);

  //=//// BEGIN MBEDTLS CODE REQUIRING CLEANUP /////////////////////////////=//

    // The memory allocated by these parts will not be automatically freed
    // in case of an error, so the code below must jump to cleanup on failure

    Value* error = nullptr;
    Value* result = nullptr;

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

    // Translate BINARY! public components to mbedtls BigNums
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

  blockscope {
    size_t key_size = mbedtls_rsa_get_len(&ctx);

    // !!! This makes a copy of the data being encrypted.  The API should
    // likely offer "raw" data access under some constraints (e.g. locking
    // the data from relocation or resize).
    //
    size_t encrypted_size;
    unsigned char* encrypted = rebBytes(&encrypted_size, rebArgR("data"));
    assert(encrypted_size == key_size);

    // rebAlloc()'d buffers can be rebRepossess()'d as a BINARY!
    //
    unsigned char* decrypted = rebAllocN(unsigned char, key_size);

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

    rebFree(encrypted);

    result = rebRepossess(decrypted, decrypted_size);
  }

  cleanup:
    mbedtls_mpi_free(&DP);
    mbedtls_mpi_free(&DQ);
    mbedtls_mpi_free(&QP);

    mbedtls_mpi_free(&D);
    mbedtls_mpi_free(&P);
    mbedtls_mpi_free(&Q);

    mbedtls_mpi_free(&N);
    mbedtls_mpi_free(&E);

    rebRelease(dp);
    rebRelease(dq);
    rebRelease(qinv);

    rebRelease(d);
    rebRelease(p);
    rebRelease(q);

    rebRelease(n);
    rebRelease(e);

    mbedtls_rsa_free(&ctx);

    if (error)
        rebJumps ("fail", error);

    return result;
}


//
//  export dh-generate-keypair: native [
//
//  "Generate a new Diffie-Hellman private/public key pair"
//
//      return: "Diffie-Hellman object with [MODULUS PRIVATE-KEY PUBLIC-KEY]"
//          [object!]
//      modulus "Public 'p', best if https://en.wikipedia.org/wiki/Safe_prime"
//          [binary!]
//      base "Public 'g', generator, less than modulus and usually prime"
//          [binary!]
//      /insecure "Don't raise errors if base/modulus choice becomes suspect"
//  ]
//
DECLARE_NATIVE(dh_generate_keypair)
{
    CRYPT_INCLUDE_PARAMS_OF_DH_GENERATE_KEYPAIR;

    struct mbedtls_dhm_context ctx;
    mbedtls_dhm_init(&ctx);

    mbedtls_mpi G;
    mbedtls_mpi P;
    mbedtls_mpi_init(&G);
    mbedtls_mpi_init(&P);

    mbedtls_mpi X;
    mbedtls_mpi_init(&X);

    Value* result = nullptr;
    Value* error = nullptr;

    bool insecure = rebDid(rebArgR("insecure"));

    Value* modulus = rebArg("modulus");
    Value* base = rebArg("base");

    size_t P_size;  // goto for cleanup would warn on crossing initialization

    // Set the prime modulus and generator.
    //
    IF_NOT_0(cleanup, error, Mpi_From_Binary(&G, base));
    IF_NOT_0(cleanup, error, Mpi_From_Binary(&P, modulus));

    P_size = mbedtls_mpi_size(&P);  // can't declare here, goto jumps across

    // !!! OpenSSL includes a DH_check() routine that checks for suitability
    // of the Diffie Hellman parameters.  There doesn't appear to be an
    // equivalent in mbedTLS at time of writing.  It might be nice to add all
    // the checks if /INSECURE is not used--or should /UNCHECKED be different?
    //
    // https://github.com/openssl/openssl/blob/master/crypto/dh/dh_check.c

    // The algorithms theoretically can work with a base greater than the
    // modulus.  But mbedTLS isn't expecting that, so you can get errors on
    // some cases and not others.  We'll pay the cost of validating that you
    // are not doing it (mbedTLS does not check--and lets you get away with
    // it sometimes, but not others).
    //
    if (mbedtls_mpi_cmp_mpi(&G, &P) >= 0) {
        error = rebValue("make error! ["
            "{Don't use base >= modulus in Diffie-Hellman.}",
            "{e.g. `2 mod 7` is the same as `9 mod 7` or `16 mod 7`}"
        "]");
        goto cleanup;
    }

    IF_NOT_0(cleanup, error, mbedtls_dhm_set_group(&ctx, &P, &G));

    // If you remove all the leading #{00} bytes from `P`, then the private
    // and public keys will be guaranteed to be no larger than that (due to
    // being `mod P`, they'll always be less).  The implementation might
    // want to ask for the smaller size, or a bigger size if more arithmetic
    // or padding is planned later on those keys.  Just use `p_size` for now.
    //
  blockscope {
    size_t x_size = P_size;
    size_t gx_size = P_size;

    // We will put the private and public keys into memory that can be
    // rebRepossess()'d as the memory backing a BINARY! series.  (This memory
    // will be automatically freed in case of a FAIL call.)
    //
    unsigned char* gx = rebAllocN(unsigned char, gx_size);  // gx => public key
    unsigned char* x = rebAllocN(unsigned char, x_size);  // x => private key

    // The "make_public" routine expects to be giving back a public key as
    // bytes, so it takes that buffer for output.  But it keeps the private
    // key in the context...so we have to extract that separately.
    //
  try_again_even_if_poor_primes: ;  // semicolon needed before declaration
    int ret = mbedtls_dhm_make_public(
        &ctx,
        x_size,  // x_size (size of private key, bigger may avoid compaction)
        gx,  // output buffer (for public key returned)
        gx_size,  // olen (only ctx.len needed, bigger may avoid compaction)
        &get_random,  // f_rng (random number generator function)
        nullptr  // p_rng (first parameter tunneled to f_rng--unused ATM)
    );

    // mbedTLS will notify you if it discovers the base and modulus you were
    // using is unsafe w.r.t. this attack:
    //
    // http://www.cl.cam.ac.uk/~rja14/Papers/psandqs.pdf
    // http://web.nvd.nist.gov/view/vuln/detail?vulnId=CVE-2005-2643
    //
    // It can't generically notice a-priori for large base and modulus if
    // such properties will be exposed.  So you only get this error if it
    // runs the randomized secret calculation and happens across a worrying
    // result.  But if you get such an error it means you should be skeptical
    // of using those numbers...and choose something more attack-resistant.
    //
    if (ret == MBEDTLS_ERR_DHM_BAD_INPUT_DATA) {
        if (mbedtls_mpi_cmp_int(&P, 0) == 0) {
            error = rebValue(
                "make error! {Cannot use 0 as modulus for Diffie-Hellman}"
            );
            goto cleanup;
        }

        if (insecure)
            goto try_again_even_if_poor_primes;  // for educational use only!

        error = rebValue(
            "make error! [",
                "{Suspiciously poor base and modulus usage was detected.}",
                "{It's unwise to use arbitrary primes vs. constructed ones:}",
                "{https://www.cl.cam.ac.uk/~rja14/Papers/psandqs.pdf}",
                "{/INSECURE can override (for educational purposes, only!)}",
            "]"
        );
        goto cleanup;
    }
    else if (ret == MBEDTLS_ERR_DHM_MAKE_PUBLIC_FAILED) {
        if (mbedtls_mpi_cmp_int(&P, 5) < 0) {
            error = rebValue(
                "make error! {Modulus cannot be less than 5 for Diffie-Hellman}"
            );
            goto cleanup;
        }

        // !!! Checking for safe primes is should probably be done by default,
        // but here's some code using a probabilistic test after failure.
        // It can be kept here for future consideration.  Rounds chosen to
        // scale to get 2^-80 chance of error for 4096 bits.
        //
        size_t ctx_len = mbedtls_dhm_get_len(&ctx);  // byte len, not bit len
        const int rounds = (ctx_len + 1) * 10;
        int test = mbedtls_mpi_is_prime_ext(
            &P,
            rounds,
            &get_random,
            nullptr
        );
        if (test == MBEDTLS_ERR_MPI_NOT_ACCEPTABLE) {
            error = rebValue(
                "make error! [",
                    "{Couldn't use base and modulus to generate keys.}",
                    "{Probabilistic test suggests modulus likely not prime?}"
                "]"
            );
            goto cleanup;
        }

        error = rebValue(
            "make error! [",
                "{Couldn't use base and modulus to generate keys,}",
                "{even though modulus does appear to be prime...}",
            "]"
        );
        goto cleanup;
    }
    else
        IF_NOT_0(cleanup, error, ret);

    // We actually want to expose the private key vs. keep it locked up in
    // a C structure context (we dispose the context and make new ones if
    // we need them).  So extract it into a binary.
    //
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

    rebRelease(base);
    rebRelease(modulus);
  }

  cleanup:
    mbedtls_mpi_free(&X);

    mbedtls_mpi_free(&G);
    mbedtls_mpi_free(&P);

    mbedtls_dhm_free(&ctx);  // should free any assigned bignum fields

    if (error)
        rebJumps ("fail", error);

    return result;
}


//
//  export dh-compute-secret: native [
//
//  "Compute secret from a private/public key pair and the peer's public key"
//
//      return: "Negotiated shared secret (same size as public/private keys)"
//          [binary!]
//      obj "The Diffie-Hellman key object"
//          [object!]
//      peer-key "Peer's public key"
//          [binary!]
//  ]
//
DECLARE_NATIVE(dh_compute_secret)
{
    CRYPT_INCLUDE_PARAMS_OF_DH_COMPUTE_SECRET;

    Value* obj = rebArg("obj");

    // Extract fields up front, so that if they fail we don't have to RESCUE it
    // to clean up an initialized dhm_context...
    //
    // !!! We could put the context in a HANDLE! with a cleanup function and
    // let the system take care of the cleanup in the event of a problem.
    //
    // !!! used to ensure object only had other fields SELF, PUB-KEY, G
    // otherwise gave Error(RE_EXT_CRYPT_INVALID_KEY_FIELD)
    //
    Value* modulus = rebValue("ensure binary! pick", obj, "'modulus");
    Value* generator = rebValue("ensure binary! pick", obj, "'generator");
    Value* private_key = rebValue("ensure binary! pick", obj, "'private-key");

    Value* result = nullptr;
    Value* error = nullptr;

    struct mbedtls_dhm_context ctx;
    mbedtls_dhm_init(&ctx);

    /* !!! This code used to initialize ctx.P (from "modulus"), ctx.X (from
     "private-key", and ctx.GY (from the peer's public key).  There is no
     clear way to initialize X in diffie hellman contexts, e.g. preload with
     "our secret value"... so I guess it expects you to feed it P and G */

    mbedtls_mpi G;
    mbedtls_mpi P;
    mbedtls_mpi_init(&G);
    mbedtls_mpi_init(&P);

    mbedtls_mpi X;
    mbedtls_mpi_init(&X);

    // Set the prime modulus and generator.
    //
    // !!! Previously, there was no need to set G for this operation, since we
    // already have GY.  However, there is no longer a way to set P without
    // setting G via mbedtls_dhm_set_group().
    //
    IF_NOT_0(cleanup, error, Mpi_From_Binary(&G, generator));
    IF_NOT_0(cleanup, error, Mpi_From_Binary(&P, modulus));
    rebRelease(modulus);
    rebRelease(generator);

    IF_NOT_0(cleanup, error, mbedtls_dhm_set_group(&ctx, &P, &G));

    // !!! There is no current approved way to set the X field of a DHM
    // context.  Do it in an unapproved way.
    // https://github.com/Mbed-TLS/mbedtls/issues/5818
    //
    IF_NOT_0(cleanup, error, Mpi_From_Binary(&X, private_key));
    mbedtls_mpi_copy(&ctx.MBEDTLS_PRIVATE(X), &X);  // !!! HACK
    rebRelease(private_key);

    // Note: mbedtls 3 only provides a "raw" import of the public key value of
    // the peer (G^Y), so we have to redo the logic of Mpi_From_Binary here.
    //
  blockscope {
    Value* peer_key = rebArg("peer-key");

    size_t gy_size;
    unsigned char* gy_buf = rebBytes(&gy_size, peer_key);  // is a rebMalloc()

    rebRelease(peer_key);

    int retcode = mbedtls_dhm_read_public(&ctx, gy_buf, gy_size);

    rebFree(gy_buf);  // !!! This could use a non-copying binary reader API
    IF_NOT_0(cleanup, error, retcode);
  }

  blockscope {
    size_t k_size = mbedtls_dhm_get_len(&ctx);  // same size as modulus/etc.
    unsigned char* k_buffer = rebAllocN(unsigned char, k_size);  // shared key

    size_t olen;
    int ret = mbedtls_dhm_calc_secret(
        &ctx,
        k_buffer,  // output buffer for the "shared secret" key
        k_size,  // output_size (at least ctx.len, more may avoid compaction)
        &olen,  // actual number of bytes written to `s`
        &get_random,  // f_rng random number generator
        nullptr  // p_rng parameter tunneled to f_rng (not used ATM)
    );

    // See remarks on DH-GENERATE-KEYPAIR for why this check is performed
    // unless /INSECURE is used.  *BUT* note that we deliberately don't allow
    // the cases of detectably sketchy private keys to pass by even with the
    // /INSECURE setting.  Instead, a new attempt is made.  So the only way
    // this happens is if the peer came from a less checked implementation.
    //
    // (There is no way to "try again" with unmodified mbedTLS code with a
    // suspect key to make a shared secret--it's not randomization, it is a
    // calculation.  Adding /INSECURE would require changing mbedTLS itself
    // to participate in decoding insecure keys.)
    //
    if (ret == MBEDTLS_ERR_DHM_BAD_INPUT_DATA) {
        error = rebValue(
            "make error! [",
                "{Suspiciously poor base and modulus usage was detected.}",
                "{It's unwise to use random primes vs. constructed ones.}",
                "{https://www.cl.cam.ac.uk/~rja14/Papers/psandqs.pdf}",
                "{If keys originated from Rebol, please report this!}",
            "]"
        );
        goto cleanup;
    }
    else
        IF_NOT_0(cleanup, error, ret);

    // !!! The multiple precision number system affords leading zeros, and
    // can optimize them out.  So 7 could be #{0007} or #{07}.  We could
    // pad the secret if we wanted to, but there's no obvious reason
    //
    assert(k_size >= olen);

    result = rebRepossess(k_buffer, k_size);
  }

  cleanup:
    mbedtls_mpi_free(&X);

    mbedtls_mpi_free(&P);
    mbedtls_mpi_free(&G);

    mbedtls_dhm_free(&ctx);

    if (error)
        rebJumps ("fail", error);

    return result;
}


static void cleanup_aes_ctx(const Value* v)
{
    struct mbedtls_cipher_context_t *ctx
        = rebUnboxHandle(struct mbedtls_cipher_context_t*, v);
    mbedtls_cipher_free(ctx);
    rebFree(ctx);
}


//
//  export aes-key: native [
//
//  "Encrypt/decrypt data using AES algorithm"
//
//      return: "Stream cipher context handle"
//          [handle!]
//      key [binary!]
//      iv "Optional initialization vector"
//          [binary! blank!]
//      /decrypt "Make cipher context for decryption (default is to encrypt)"
//  ]
//
DECLARE_NATIVE(aes_key)
{
    CRYPT_INCLUDE_PARAMS_OF_AES_KEY;

    size_t key_size;
    unsigned char* key_bytes = rebBytes(&key_size, rebArgR("key"));

    int key_bitlen = key_size * 8;
    if (key_bitlen != 128 and key_bitlen != 192 and key_bitlen != 256) {
        rebJumps(
            "fail [{AES bits must be [128 192 256], not}", rebI(key_bitlen), "]"
        );
    }

    const mbedtls_cipher_info_t* info = mbedtls_cipher_info_from_values(
        MBEDTLS_CIPHER_ID_AES,
        key_bitlen,
        MBEDTLS_MODE_CBC
    );

    struct mbedtls_cipher_context_t* ctx
        = rebAlloc(struct mbedtls_cipher_context_t);
    rebUnmanageMemory(ctx);  // needs to outlive this AES-KEY function call
    mbedtls_cipher_init(ctx);

    Value* error = nullptr;

    IF_NOT_0(cleanup, error, mbedtls_cipher_setup(ctx, info));

    IF_NOT_0(cleanup, error, mbedtls_cipher_setkey(
        ctx,
        key_bytes,
        key_bitlen,
        rebDid(rebArgR("decrypt")) ? MBEDTLS_DECRYPT : MBEDTLS_ENCRYPT
    ));
    rebFree(key_bytes);

    // Default padding mode is PKCS7, but TLS won't work unless you use zeros.
    // (Shown also by the %ssl_tls.c file for mbedTLS, see AES CBC ciphers.)
    //
    IF_NOT_0(cleanup, error,
        mbedtls_cipher_set_padding_mode(ctx, MBEDTLS_PADDING_NONE)
    );

  blockscope {
    size_t blocksize = mbedtls_cipher_get_block_size(ctx);
    Value* iv = rebArg("iv");
    if (rebUnboxLogic("binary?", iv)) {
        size_t iv_size;
        unsigned char* iv_bytes = rebBytes(&iv_size, iv);

        if (iv_size != blocksize) {
            error = rebValue("make error! [",
                "Initialization vector block size not", rebI(blocksize),
            "]");
            goto cleanup;
        }

        IF_NOT_0(cleanup, error, mbedtls_cipher_set_iv(ctx, iv_bytes, iv_size));
        rebFree(iv_bytes);
    }
    else
        assert(rebUnboxLogic("blank?", iv));
    rebRelease(iv);
  }

  cleanup:
    if (error) {
        mbedtls_cipher_free(ctx);
        rebJumps ("fail", error);
    }

    return rebHandle(
        ctx,
        sizeof(struct mbedtls_cipher_context_t),
        &cleanup_aes_ctx
    );
}


//
//  export aes-stream: native [
//
//  "Encrypt/decrypt data using AES algorithm"
//
//      return: "Encrypted/decrypted data (null if zero length)"
//          [<opt> binary!]
//      ctx "Stream cipher context"
//          [handle!]
//      data [binary!]
//  ]
//
DECLARE_NATIVE(aes_stream)
{
    CRYPT_INCLUDE_PARAMS_OF_AES_STREAM;

    Value* ctx_handle = rebArg("ctx");

    if (rebExtractHandleCleaner(ctx_handle) != cleanup_aes_ctx)
        rebJumps(
            "fail [{Not a AES context HANDLE!:}", ctx_handle, "]"
        );

    struct mbedtls_cipher_context_t *ctx
        = rebUnboxHandle(struct mbedtls_cipher_context_t*, ctx_handle);

    rebRelease(ctx_handle);

    size_t ilen;
    unsigned char* input = rebBytes(&ilen, rebArgR("data"));

    if (ilen == 0) {
        rebFree(input);
        return nullptr;  // !!! Is NULL a good result for 0 data?
    }

    Value* error = nullptr;
    Value* result = nullptr;

    // output data buffer must be at least the input length plus block size.
    //
    size_t blocksize = mbedtls_cipher_get_block_size(ctx);
    assert(blocksize == 16);  // !!! to be generalized...

    // !!! Saphir's AES code worked with zero-padded chunks, so you always
    // got a multiple of 16 bytes out.  That doesn't seem optimal for a
    // "streaming cipher" because for the output to be useful, your input
    // has to come pre-chunked; you should be able to add one byte at a time
    // if you want.  For starters the code is kept compatible just to excise
    // the old AES implementation--but this needs to change, maybe to
    // a PORT! model of some kind.
    //
    size_t pad_len = (((ilen - 1) >> 4) << 4) + blocksize;

    unsigned char* pad_data;
    if (ilen < pad_len) {
        //
        //  make new data input with zero-padding
        //
        pad_data = rebAllocN(unsigned char, pad_len);
        memset(pad_data, 0, pad_len);
        memcpy(pad_data, input, ilen);
        input = pad_data;
    }
    else
        pad_data = nullptr;

    unsigned char* output = rebAllocN(unsigned char, ilen + blocksize);

    size_t olen;
    IF_NOT_0(cleanup, error,
        mbedtls_cipher_update(ctx, input, pad_len, output, &olen)
    );

    rebFree(input);

    result = rebRepossess(output, olen);

  cleanup:

    rebFreeMaybe(pad_data);

    if (error)
        rebJumps ("fail", error);

    return result;
}


// For reasons that don't seem particularly good for a generic cryptography
// library that is not entirely TLS-focused, the 25519 curve isn't in the
// main list of curves:
//
// https://github.com/ARMmbed/mbedtls/issues/464
//
struct mbedtls_ecp_curve_info curve25519_info =
    { MBEDTLS_ECP_DP_CURVE25519, 29, 256, "curve25519"};


static const struct mbedtls_ecp_curve_info* Ecp_Curve_Info_From_Word(
    const Value* word
){
    const struct mbedtls_ecp_curve_info *info;

    if (rebUnboxLogic("'curve25519 = @", word))
        info = &curve25519_info;
    else {
        char* name = rebSpell("lowercase to text! @", word);
        info = mbedtls_ecp_curve_info_from_name(name);
        rebFree(name);
    }

    if (not info)
        rebJumps (
            "fail [{Unknown ECC curve specified:} @", word, "]"
        );

    return info;
}


//
//  export ecc-generate-keypair: native [
//
//  "Generates an uncompressed secp256r1 key"
//
//      return: "object with PUBLIC/X, PUBLIC/Y, and PRIVATE key members"
//          [object!]
//      group "Elliptic curve group [CURVE25519 SECP256R1 ...]"
//          [word!]
//  ]
//
DECLARE_NATIVE(ecc_generate_keypair)
//
// !!! Note: using curve25519 seems to always give a y coordinate of zero
// in the public key.  Is this correct (it seems to yield the right secret)?
{
    CRYPT_INCLUDE_PARAMS_OF_ECC_GENERATE_KEYPAIR;

    // A change in mbedTLS ecdh code means there's a context variable in
    // the context (ctx.ctx) when not using MBEDTLS_ECDH_LEGACY_CONTEXT
    //
    struct mbedtls_ecdh_context ctx;
    mbedtls_ecdh_init(&ctx);

    Value* error = nullptr;
    Value* result = nullptr;

    Value* group = rebArg("group");
    const struct mbedtls_ecp_curve_info *info = Ecp_Curve_Info_From_Word(group);
    rebRelease(group);

    size_t num_bytes = info->bit_size / 8;

    IF_NOT_0(cleanup, error,
        mbedtls_ecdh_setup(&ctx, info->grp_id)
    );

    // !!! The mbedtls 3.0 transition has not established a way to get at
    // the private fields via functions.  They cheat via MBEDTLS_PRIVATE.
    // https://github.com/Mbed-TLS/mbedtls/issues/5016
    //
    mbedtls_ecdh_context_mbed *mbed_ecdh;  // goto crosses initialization
    mbed_ecdh = &ctx.MBEDTLS_PRIVATE(ctx).MBEDTLS_PRIVATE(mbed_ecdh);

    IF_NOT_0(cleanup, error, mbedtls_ecdh_gen_public(
        &mbed_ecdh->MBEDTLS_PRIVATE(grp),
        &mbed_ecdh->MBEDTLS_PRIVATE(d),  // private key
        &mbed_ecdh->MBEDTLS_PRIVATE(Q),  // public key (X, Y)
        &get_random,  // f_rng, random number generator
        nullptr  // p_rng, parameter tunneled to random generator (unused atm)
    ));

    // Allocate into memory that can be retaken directly as BINARY! in Rebol
    //
  blockscope {
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
  }

  cleanup:
    mbedtls_ecdh_free(&ctx);

    if (error)
        rebJumps ("fail", error);

    return result;
}


//
//  export ecdh-shared-secret: native [
//      return: "secret"
//          [binary!]
//      group "Elliptic curve group [CURVE25519 SECP256R1 ...]"
//          [word!]
//      private "32-byte private key"
//          [binary!]
//      public "64-byte public key of peer (or OBJECT! with 32-byte X and Y)"
//          [binary! object!]
//  ]
//
DECLARE_NATIVE(ecdh_shared_secret)
{
    CRYPT_INCLUDE_PARAMS_OF_ECDH_SHARED_SECRET;

    Value* group = rebArg("group");
    const struct mbedtls_ecp_curve_info *info = Ecp_Curve_Info_From_Word(group);
    // `group` used in various later code, release later

    Value* private_arg = rebArg("private");  // `private` is C++ keyword
    Value* public_arg = rebArg("public");  // `public` is C++ keyword

    size_t num_bytes = info->bit_size / 8;

    unsigned char* public_key = rebAllocN(unsigned char, num_bytes * 2);

    rebBytesInto(public_key, num_bytes * 2, "use [bin] [",
        "bin: either binary?", public_arg, "[", public_arg, "] [",
            "append copy pick", public_arg, "'x", "pick", public_arg, "'y"
        "]",
        "if", rebI(num_bytes * 2), "!= length of bin [",
            "fail [{Public BINARY! must be}", rebI(num_bytes * 2),
                "{bytes total for} @", group, "]",
        "]",
        "bin",
    "]");
    rebRelease(public_arg);

    // A change in mbedTLS ecdh code means there's a context variable in
    // the context (ctx.ctx) when not using MBEDTLS_ECDH_LEGACY_CONTEXT
    //
    struct mbedtls_ecdh_context ctx;
    mbedtls_ecdh_init(&ctx);

    Value* result = nullptr;
    Value* error = nullptr;

    IF_NOT_0(cleanup, error,
        mbedtls_ecdh_setup(&ctx, info->grp_id)
    );

    // !!! The mbedtls 3.0 transition has not established a way to get at
    // the private fields via functions.  They cheat via MBEDTLS_PRIVATE.
    // https://github.com/Mbed-TLS/mbedtls/issues/5016
    //
    mbedtls_ecdh_context_mbed *mbed_ecdh;  // goto crosses initialization
    mbed_ecdh = &ctx.MBEDTLS_PRIVATE(ctx).MBEDTLS_PRIVATE(mbed_ecdh);

    IF_NOT_0(cleanup, error, mbedtls_mpi_read_binary(
        &mbed_ecdh->MBEDTLS_PRIVATE(Qp).MBEDTLS_PRIVATE(X),
        public_key,
        num_bytes
    ));
    IF_NOT_0(cleanup, error, mbedtls_mpi_read_binary(
        &mbed_ecdh->MBEDTLS_PRIVATE(Qp).MBEDTLS_PRIVATE(Y),
        public_key + num_bytes,
        num_bytes
    ));
    IF_NOT_0(cleanup, error, mbedtls_mpi_lset(
        &mbed_ecdh->MBEDTLS_PRIVATE(Qp).MBEDTLS_PRIVATE(Z),
        1
    ));

    rebElide(
        "if", rebI(num_bytes), "!= length of", private_arg, "[",
            "fail [{Size of PRIVATE key must be}",
                rebI(num_bytes), "{for} @", group, "]"
        "]"
    );

    IF_NOT_0(cleanup, error,
        Mpi_From_Binary(&mbed_ecdh->MBEDTLS_PRIVATE(d), private_arg));

    rebRelease(private_arg);
    rebRelease(group);

  blockscope {
    uint8_t *secret = rebAllocN(uint8_t, num_bytes);
    size_t olen;
    IF_NOT_0(cleanup, error, mbedtls_ecdh_calc_secret(
        &ctx,
        &olen,
        secret,
        num_bytes,
        &get_random,
        nullptr
    ));
    assert(olen == num_bytes);
    result = rebRepossess(secret, num_bytes);
  }

  cleanup:
    rebFree(public_key);
    mbedtls_ecdh_free(&ctx);

    if (error)
        rebJumps ("fail", error);

    return result;
}

EXTERN_C int tf_snprintf(char* s, size_t n, const char* fmt, ...);


//
//  startup*: native [
//
//  "Initialize random number generators and OS-provided crypto services"
//
//      return: [~]
//  ]
//
DECLARE_NATIVE(startup_p)
{
    CRYPT_INCLUDE_PARAMS_OF_STARTUP_P;

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
    rebJumps (
        "fail {Crypto STARTUP* couldn't initialize random number generation}"
    );
}


//
//  shutdown*: native [
//
//  "Shut down random number generators and OS-provided crypto services"
//
//      return: [~]
//  ]
//
DECLARE_NATIVE(shutdown_p)
{
    CRYPT_INCLUDE_PARAMS_OF_SHUTDOWN_P;

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
