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
// Copyright 2012-2020 Ren-C Open Source Contributors
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

#ifdef TO_WINDOWS
    #undef _WIN32_WINNT  // https://forum.rebol.info/t/326/4
    #define _WIN32_WINNT 0x0501  // Minimum API target: WinXP
    #define WIN32_LEAN_AND_MEAN  // trim down the Win32 headers
    #include <windows.h>
    #include <wincrypt.h>

    #undef IS_ERROR  // %windows.h defines this, but so does %sys-core.h

    #undef min
    #undef max
#else
    #include <fcntl.h>
    #include <unistd.h>
#endif

#include "sys-core.h"

#include "mbedtls/dhm.h"  // Diffie-Hellman (credits Merkel, by their request)

#include "mbedtls/arc4.h"  // RC4 is technically trademarked, so it's "ARC4"

#include "sys-zlib.h"  // needed for the ADLER32 hash

#include "tmp-mod-crypt.h"



// Most routines in mbedTLS return either `void` or an `int` code which is
// 0 on success and negative numbers on error.  This macro helps generalize
// the pattern of trying to build a result and having a cleanup (similar
// ones exist inside mbedTLS itself, e.g. MBEDTLS_MPI_CHK() in %bignum.h)
//
// !!! We probably do not need to have non-debug builds use up memory by
// integrating the string table translating all those negative numbers into
// specific errors.  But a debug build might want to.  For now, one error.
//
#define IF_NOT_0(label,error,call) \
    do { \
        assert(error == nullptr); \
        int mbedtls_ret = (call);  /* don't use (call) more than once! */ \
        if (mbedtls_ret != 0) { \
            error = rebValue("make error! {mbedTLS error}", rebEND); \
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
//     int (*f_rng)(void *p_rng, unsigned char *output, size_t len);
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
#ifdef TO_WINDOWS
    HCRYPTPROV gCryptProv = 0;
#else
    int rng_fd = -1;
#endif

int get_random(void *p_rng, unsigned char *output, size_t output_len)
{
    assert(p_rng == nullptr);  // parameter currently not used
    UNUSED(p_rng);

  #ifdef TO_WINDOWS
    if (CryptGenRandom(gCryptProv, output_len, output) != 0)
        return 0;  // success
  #else
    if (rng_fd != -1 && read(rng_fd, output, output_len) != -1)
        return 0;  // success
  #endif

  rebJumps ("fail {Random number generation did not succeed}", rebEND);
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
//  export checksum: native [
//
//  "Computes a checksum, CRC, or hash."
//
//      return: "Warning: likely to be changed to always be BINARY!"
//          [binary! integer!]  ; see note below
//      :settings "Temporarily literal word, evaluative after /METHOD purged"
//          [<skip> lit-word!]
//      data "Input data to digest (TEXT! is interpreted as UTF-8 bytes)"
//          [binary! text!]
//      /part "Length of data to use, default is current index to series end"
//          [any-value!]
//      /method "Supply a method name (deprecated, use `settings`)"
//          [word!]
//      /key "Returns keyed HMAC value"
//          [binary! text!]
//  ]
//
REBNATIVE(checksum)
//
// !!! The /METHOD refinement is being removed because you pretty much always
// need to supply a method.  As an interim compatibility measure, it is kept
// but the preference is to say e.g. `checksum 'sha256 data`.
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
// !!! There was a /HASH option that took an integer and claimed to "return
// a hash value with given size".  But what it did was:
//
//    REBINT sum = VAL_INT32(ARG(hash));
//    if (sum <= 1)
//        sum = 1;
//    Init_Integer(D_OUT, Hash_Bytes(data, len) % sum);
//
// As nothing used it, it's not clear what this was for.  Currently removed.
{
    CRYPT_INCLUDE_PARAMS_OF_CHECKSUM;

    Dequotify(ARG(settings));

    REBLEN len = Part_Len_May_Modify_Index(ARG(data), ARG(part));

    REBSIZ size;
    const REBYTE *data = VAL_BYTES_LIMIT_AT(&size, ARG(data), len);

    // Turn the method into a string and look it up in the table that mbedTLS
    // builds in when you `#include "md.h"`.  How many entries are in this
    // table depend on the config settings (see %mbedtls-rebol-config.h)
    //
    char *method_name = rebSpellQ(
        "all [", REF(method), REF(settings), "] then [",
            "fail {Specify SETTINGS or /METHOD for CHECKSUM, not both}",
        "]",
        "uppercase try to text! try any [", REF(method), REF(settings), "]",
    rebEND);
    if (method_name == nullptr)
        fail ("Must specify SETTINGS for CHECKSUM");

    const mbedtls_md_info_t *info = mbedtls_md_info_from_string(method_name);
    if (info) {
        rebFree(method_name);
        goto found_tls_info;
    }

    if (REF(key))  // old methods do not support HMAC keying
        rebJumps ("fail {/METHOD does not support HMAC keying}", rebEND);

    // Look up some internally available methods.
    //
    if (0 == strcmp(method_name, "CRC24")) {
        rebFree(method_name);
        Init_Integer(D_SPARE, Compute_CRC24(data, size));
        return rebValue("enbin [le + 3]", D_SPARE, rebEND);
    }
    if (0 == strcmp(method_name, "CRC32")) {
        //
        // CRC32 is a hash needed for gzip which is a sunk cost, and it
        // was exposed in R3-Alpha.  It is typically an unsigned 32-bit
        // number and uses the full range of values.  Yet R3-Alpha chose to
        // export this as a signed integer via CHECKSUM, presumably to
        // generate a value that could be used by Rebol2, as it only had
        // 32-bit signed INTEGER!.
        //
        rebFree(method_name);
        Init_Integer(D_SPARE, crc32_z(0L, data, size));
        return rebValue("enbin [le + 4]", D_SPARE, rebEND);
    }
    else if (0 == strcmp(method_name, "ADLER32")) {
        //
        // ADLER32 is a hash available in zlib which is a sunk cost, so
        // it was exposed by Saphirion.  That happened after 64-bit
        // integers were available, and did not convert the unsigned
        // result of the adler calculation to a signed integer.
        //
        rebFree(method_name);
        Init_Integer(D_SPARE, z_adler32(1L, data, size));  // Note the 1L (!)
        return rebValue("enbin [le + 4]", D_SPARE, rebEND);
    }
    else if (0 == strcmp(method_name, "TCP")) {
        //
        // !!! This was an "Internet TCP 16-bit checksum" that was initially
        // a refinement (presumably because adding table entries was a pain).
        // It does not seem to be used?
        //
        rebFree(method_name);
        Init_Integer(D_SPARE, Compute_IPC(data, size));
        return rebValue("enbin [le + 2]", D_SPARE, rebEND);
    }

    rebJumps (
        "fail [{Unknown CHECKSUM method:}", rebQ(ARG(method)), "]",
    rebEND);

  found_tls_info: {
    int hmac = REF(key) ? 1 : 0;  // !!! int, but seems to be a boolean?

    unsigned char md_size = mbedtls_md_get_size(info);
    REBYTE *output = rebAllocN(REBYTE, md_size);

    REBVAL *error = nullptr;
    REBVAL *result = nullptr;

    struct mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    IF_NOT_0(cleanup, error, mbedtls_md_setup(&ctx, info, hmac));

    if (hmac) {
        REBSIZ key_size;
        const REBYTE *key_bytes = VAL_BYTES_AT(&key_size, ARG(key));

        IF_NOT_0(cleanup, error,
            mbedtls_md_hmac_starts(&ctx, key_bytes, key_size)
        );
        IF_NOT_0(cleanup, error, mbedtls_md_hmac_update(&ctx, data, size));
        IF_NOT_0(cleanup, error, mbedtls_md_hmac_finish(&ctx, output));
    }
    else {
        IF_NOT_0(cleanup, error, mbedtls_md_starts(&ctx));
        IF_NOT_0(cleanup, error, mbedtls_md_update(&ctx, data, size));
        IF_NOT_0(cleanup, error, mbedtls_md_finish(&ctx, output));
    }

    result = rebRepossess(output, md_size);

  cleanup:
    mbedtls_md_free(&ctx);
    if (error)
        rebJumps ("fail", error, rebEND);

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


static void cleanup_rc4_ctx(const REBVAL *v)
{
    struct mbedtls_arc4_context *ctx
        = VAL_HANDLE_POINTER(struct mbedtls_arc4_context, v);
    mbedtls_arc4_free(ctx);
    FREE(struct mbedtls_arc4_context, ctx);
}


//
//  export rc4-key: native [
//
//  "Encrypt/decrypt data (modifies) using RC4 algorithm."
//
//      return: [handle!]
//      key [binary!]
//  ]
//
REBNATIVE(rc4_key)
//
// !!! RC4 was originally included for use with TLS.  However, the insecurity
// of RC4 led the IETF to prohibit RC4 for TLS use in 2015:
//
// https://tools.ietf.org/html/rfc7465
//
// So it is not in use at the moment.  It isn't much code, but could probably
// be moved to its own extension so it could be selected to build in or not,
// which is how cryptography methods should probably be done.
{
    CRYPT_INCLUDE_PARAMS_OF_RC4_KEY;

    struct mbedtls_arc4_context *ctx = TRY_ALLOC(struct mbedtls_arc4_context);
    mbedtls_arc4_init(ctx);

    REBSIZ key_len;
    const REBYTE *key = VAL_BINARY_SIZE_AT(&key_len, ARG(key));
    mbedtls_arc4_setup(ctx, key, key_len);

    return Init_Handle_Cdata_Managed(
        D_OUT,
        ctx,
        sizeof(struct mbedtls_arc4_context),
        &cleanup_rc4_ctx
    );
}


//
//  export rc4-stream: native [
//
//  "Encrypt/decrypt data (modifies) using RC4 algorithm."
//
//      return: <void>
//      ctx "Stream cipher context"
//          [handle!]
//      data "Data to encrypt/decrypt (modified)"
//          [binary!]
//  ]
//
REBNATIVE(rc4_stream)
{
    CRYPT_INCLUDE_PARAMS_OF_RC4_STREAM;

    REBVAL *data = ARG(data);

    if (VAL_HANDLE_CLEANER(ARG(ctx)) != cleanup_rc4_ctx)
        rebJumps ("fail [{Not a RC4 Context:}", ARG(ctx), "]", rebEND);

    struct mbedtls_arc4_context *ctx
        = VAL_HANDLE_POINTER(struct mbedtls_arc4_context, ARG(ctx));

    REBVAL *error = nullptr;

    REBSIZ length;
    REBYTE *output = VAL_BINARY_SIZE_AT_ENSURE_MUTABLE(&length, data);
    const REBYTE *input = output;
    IF_NOT_0(cleanup, error, mbedtls_arc4_crypt(
        ctx,
        length,
        input,  // input "message"
        output  // output (same, since it modifies)
    ));

  cleanup:
     if (error)
        rebJumps ("fail", error, rebEND);

    return rebVoid();
}


// For turning a BINARY! into an mbedTLS multiple-precision-integer ("bignum")
// Returns an mbedTLS error code if there is a problem (use with IF_NOT_0)
//
static int Mpi_From_Binary(mbedtls_mpi* X, const REBVAL *binary)
{
    size_t size;
    REBYTE *buf = rebBytes(&size, binary, rebEND);  // allocates w/rebMalloc()

    int result = mbedtls_mpi_read_binary(X, buf, size);

    assert(mbedtls_mpi_size(X) == size);  // should reflect what we set
    rebFree(buf);  // !!! This could use a non-copying binary reader API

    return result;
}


//
//  export rsa: native [
//
//  "Encrypt/decrypt data using the RSA algorithm."
//
//      data [binary!]
//      key-object [object!]
//      /decrypt "Decrypts the data (default is to encrypt)"
//      /private "Uses an RSA private key (default is a public key)"
//  ]
//
REBNATIVE(rsa)
{
    CRYPT_INCLUDE_PARAMS_OF_RSA;

    REBVAL *obj = ARG(key_object);

    // N and E are required
    //
    REBVAL *n = rebValue("ensure binary! pick", obj, "'n", rebEND);
    REBVAL *e = rebValue("ensure binary! pick", obj, "'e", rebEND);

    int hash_id = MBEDTLS_MD_NONE;  // could pass a hash here...
    struct mbedtls_rsa_context ctx;
    mbedtls_rsa_init(&ctx, MBEDTLS_RSA_PKCS_V15, hash_id);

    REBVAL *error = nullptr;
    REBVAL *result = nullptr;

    REBSIZ binary_len;  // goto would cross initialization

    // Public exponents - required
    //
    IF_NOT_0(cleanup, error, Mpi_From_Binary(&ctx.N, n));
    IF_NOT_0(cleanup, error, Mpi_From_Binary(&ctx.E, e));

    binary_len = rebUnboxInteger("length of", n, rebEND);
    ctx.len = binary_len;
    rebRelease(n);
    rebRelease(e);

    if (REF(private)) {
        REBVAL *d = rebValue("ensure binary! pick", obj, "'d", rebEND);

        if (not d)
            fail ("No d returned BLANK, can we assume error for cleanup?");

        REBVAL *p = rebValue("ensure binary! pick", obj, "'p", rebEND);
        REBVAL *q = rebValue("ensure binary! pick", obj, "'q", rebEND);
        REBVAL *dp = rebValue("ensure binary! pick", obj, "'dp", rebEND);
        REBVAL *dq = rebValue("ensure binary! pick", obj, "'dq", rebEND);
        REBVAL *qinv = rebValue("ensure binary! pick", obj, "'qinv", rebEND);

        binary_len = rebUnbox("length of", d, rebEND);

        IF_NOT_0(cleanup, error, Mpi_From_Binary(&ctx.D, d));

        if (p)
            IF_NOT_0(cleanup, error, Mpi_From_Binary(&ctx.P, p));
        if (q)
            IF_NOT_0(cleanup, error, Mpi_From_Binary(&ctx.Q, q));
        if (dp)
            IF_NOT_0(cleanup, error, Mpi_From_Binary(&ctx.DP, dp));
        if (dq)
            IF_NOT_0(cleanup, error, Mpi_From_Binary(&ctx.DQ, dq));
        if (qinv)
            IF_NOT_0(cleanup, error, Mpi_From_Binary(&ctx.QP, qinv));

        IF_NOT_0(cleanup, error, mbedtls_rsa_gen_key(
            &ctx,
            &get_random,  // f_rng, random number generating function
            nullptr,  // tunneled parameter to f_rng (unused atm, so nullptr)
            binary_len * 8,  // number of bits in key
            65537  // this is what mbedTLS %gen_key.c uses for exponent (?)
        ));

        rebRelease(d);
        rebRelease(p);
        rebRelease(q);
        rebRelease(dp);
        rebRelease(dq);
        rebRelease(qinv);
    }

  blockscope {
    //
    // !!! This makes a copy of the data being encrypted.  The API should
    // likely offer "raw" data access under some constraints (e.g. locking
    // the data from relocation or resize).
    //
    REBSIZ data_len;
    REBYTE *dataBuffer = rebBytes(&data_len, ARG(data), rebEND);

    // Buffer suitable for recapturing as a BINARY! for either the encrypted
    // or decrypted data
    //
    REBYTE *crypted = rebAllocN(REBYTE, binary_len);

    if (REF(decrypt)) {
        size_t olen;
        IF_NOT_0(cleanup, error, mbedtls_rsa_pkcs1_decrypt(
            &ctx,
            &get_random,
            nullptr,
            REF(private) ? MBEDTLS_RSA_PRIVATE : MBEDTLS_RSA_PUBLIC,
            &olen,
            dataBuffer,
            crypted,
            binary_len
        ));
        assert(olen == binary_len);
    }
    else {
        IF_NOT_0(cleanup, error, mbedtls_rsa_pkcs1_encrypt(
            &ctx,
            &get_random,
            nullptr,
            REF(private) ? MBEDTLS_RSA_PRIVATE : MBEDTLS_RSA_PUBLIC,
            data_len,
            dataBuffer,
            crypted
        ));
    }

    rebFree(dataBuffer);

    result = rebRepossess(crypted, binary_len);
  }

  cleanup:
    mbedtls_rsa_free(&ctx);
    if (error)
        rebJumps ("fail", error, rebEND);

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
REBNATIVE(dh_generate_keypair)
{
    CRYPT_INCLUDE_PARAMS_OF_DH_GENERATE_KEYPAIR;

    REBVAL *g = ARG(base);
    REBVAL *p = ARG(modulus);

    struct mbedtls_dhm_context ctx;
    mbedtls_dhm_init(&ctx);

    REBVAL *result = nullptr;
    REBVAL *error = nullptr;

    REBSIZ p_size;  // goto would cross initialization

    // We avoid calling mbedtls_dhm_set_group() to assign the `G`, `P`, and
    // `len` fields, to not need intermediate mbedtls_mpi variables.  At time
    // of writing the code is equivalent--but if this breaks, use that method.
    //
    IF_NOT_0(cleanup, error, Mpi_From_Binary(&ctx.G, g));
    IF_NOT_0(cleanup, error, Mpi_From_Binary(&ctx.P, p));

    p_size = mbedtls_mpi_size(&ctx.P);
    ctx.len = p_size;  // length of private and public keys

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
    if (mbedtls_mpi_cmp_mpi(&ctx.G, &ctx.P) >= 0)
        rebJumps (
            "fail [",
                "{Don't use base >= modulus in Diffie-Hellman.}",
                "{e.g. `2 mod 7` is the same as `9 mod 7` or `16 mod 7`}",
            "]",
        rebEND);

    // If you remove all the leading #{00} bytes from `p`, then the private
    // and public keys will be guaranteed to be no larger than that (due to
    // being `mod p`, they'll always be less).  The implementation might
    // want to ask for the smaller size, or a bigger size if more arithmetic
    // or padding is planned later on those keys.  Just use `p_size` for now.
    //
  blockscope {
    REBLEN x_size = p_size;
    REBLEN gx_size = p_size;

    // We will put the private and public keys into memory that can be
    // rebRepossess()'d as the memory backing a BINARY! series.  (This memory
    // will be automatically freed in case of a FAIL call.)
    //
    REBYTE *gx = rebAllocN(REBYTE, gx_size);  // gx => public key
    REBYTE *x = rebAllocN(REBYTE, x_size);  // x => private key

    // The "make_public" routine expects to be giving back a public key as
    // bytes, so it takes that buffer for output.  But it keeps the private
    // key inside the context...so we have to extract that separately.
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
        if (mbedtls_mpi_cmp_int(&ctx.P, 0) == 0)
            rebJumps (
                "fail {Cannot use 0 as modulus for Diffie-Hellman}",
            rebEND);

        if (REF(insecure))
            goto try_again_even_if_poor_primes;  // for educational use only!

        rebJumps (
            "fail [",
                "{Suspiciously poor base and modulus usage was detected.}",
                "{It's unwise to use arbitrary primes vs. constructed ones:}",
                "{https://www.cl.cam.ac.uk/~rja14/Papers/psandqs.pdf}",
                "{/INSECURE can override (for educational purposes, only!)}",
            "]",
            rebEND);
    }
    else if (ret == MBEDTLS_ERR_DHM_MAKE_PUBLIC_FAILED) {
        if (mbedtls_mpi_cmp_int(&ctx.P, 5) < 0)
            rebJumps (
                "fail {Modulus cannot be less than 5 for Diffie-Hellman}",
            rebEND);

        // !!! Checking for safe primes is should probably be done by default,
        // but here's some code using a probabilistic test after failure.
        // It can be kept here for future consideration.  Rounds chosen to
        // scale to get 2^-80 chance of error for 4096 bits.
        //
        const int rounds = ((ctx.len / 32) + 1) * 10;
        int test = mbedtls_mpi_is_prime_ext(
            &ctx.P,
            rounds,
            &get_random,
            nullptr
        );
        if (test == MBEDTLS_ERR_MPI_NOT_ACCEPTABLE) {
            rebJumps (
                "fail [",
                    "{Couldn't use base and modulus to generate keys.}",
                    "{Probabilistic test suggests modulus likely not prime?}"
                "]",
                rebEND);
        }

        rebJumps (
            "fail [",
                "{Couldn't use base and modulus to generate keys,}",
                "{even though modulus does appear to be prime...}",
            "]",
            rebEND);
    }
    else
        IF_NOT_0(cleanup, error, ret);

    // We actually want to expose the private key vs. keep it locked up in
    // a C structure context (we dispose the context and make new ones if
    // we need them).  So extract it into a binary.
    //
    IF_NOT_0(cleanup, error, mbedtls_mpi_write_binary(&ctx.X, x, x_size));

    result = rebValue(
        "make object! [",
            "modulus:", p,
            "private-key:", rebR(rebRepossess(x, x_size)),
            "public-key:", rebR(rebRepossess(gx, gx_size)),
        "]",
    rebEND);
  }

  cleanup:
    mbedtls_dhm_free(&ctx);  // should free any assigned bignum fields

    if (error)
        rebJumps ("fail", error, rebEND);

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
REBNATIVE(dh_compute_secret)
{
    CRYPT_INCLUDE_PARAMS_OF_DH_COMPUTE_SECRET;

    REBVAL *obj = ARG(obj);

    // Extract fields up front, so that if they fail we don't have to TRAP it
    // to clean up an initialized dhm_context...
    //
    // !!! used to ensure object only had other fields SELF, PUB-KEY, G
    // otherwise gave Error(RE_EXT_CRYPT_INVALID_KEY_FIELD)
    //
    REBVAL *p = rebValue("ensure binary! pick", obj, "'modulus", rebEND);
    REBVAL *x = rebValue("ensure binary! pick", obj, "'private-key", rebEND);

    REBVAL *gy = ARG(peer_key);

    REBVAL *result = nullptr;
    REBVAL *error = nullptr;

    REBSIZ p_size;  // goto would cross initialization

    struct mbedtls_dhm_context ctx;
    mbedtls_dhm_init(&ctx);

    IF_NOT_0(cleanup, error, Mpi_From_Binary(&ctx.P, p));
    rebRelease(p);

    p_size = mbedtls_mpi_size(&ctx.P);
    ctx.len = p_size;  // length of private and public keys

    IF_NOT_0(cleanup, error, Mpi_From_Binary(&ctx.X, x));
    rebRelease(x);

    IF_NOT_0(cleanup, error, Mpi_From_Binary(&ctx.GY, gy));

  blockscope {
    REBLEN s_size = ctx.len;  // shared key is same size as modulus/etc.
    REBYTE *s = rebAllocN(REBYTE, s_size);  // shared key buffer

    size_t olen;
    int ret = mbedtls_dhm_calc_secret(
        &ctx,
        s,  // output buffer for the "shared secret" key
        s_size,  // output_size (at least ctx.len, more may avoid compaction)
        &olen,  // actual number of bytes written to `k`
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
        rebJumps (
            "fail [",
                "{Suspiciously poor base and modulus usage was detected.}",
                "{It's unwise to use random primes vs. constructed ones.}",
                "{https://www.cl.cam.ac.uk/~rja14/Papers/psandqs.pdf}",
                "{If keys originated from Rebol, please report this!}",
            "]",
            rebEND);
    }
    else
        IF_NOT_0(cleanup, error, ret);

    // !!! The multiple precision number system affords leading zeros, and
    // can optimize them out.  So 7 could be #{0007} or #{07}.  We could
    // pad the secret if we wanted to, but there's no obvious reason
    //
    assert(s_size >= olen);

    result = rebRepossess(s, s_size);
  }

  cleanup:
    mbedtls_dhm_free(&ctx);

    if (error)
        rebJumps ("fail", error, rebEND);

    return result;
}


static void cleanup_aes_ctx(const REBVAL *v)
{
    struct mbedtls_cipher_context_t *ctx
        = VAL_HANDLE_POINTER(struct mbedtls_cipher_context_t, v);
    mbedtls_cipher_free(ctx);
    FREE(struct mbedtls_cipher_context_t, ctx);
}


//
//  export aes-key: native [
//
//  "Encrypt/decrypt data using AES algorithm."
//
//      return: "Stream cipher context handle"
//          [handle!]
//      key [binary!]
//      iv "Optional initialization vector"
//          [binary! blank!]
//      /decrypt "Make cipher context for decryption (default is to encrypt)"
//  ]
//
REBNATIVE(aes_key)
{
    CRYPT_INCLUDE_PARAMS_OF_AES_KEY;

    REBSIZ p_size;
    REBYTE *p_key = rebBytes(&p_size, ARG(key), rebEND);

    REBINT keybits = p_size * 8;
    if (keybits != 128 and keybits != 192 and keybits != 256) {
        DECLARE_LOCAL (i);
        Init_Integer(i, keybits);
        rebJumps(
            "fail [{AES bits must be [128 192 256], not}", rebI(keybits), "]",
        rebEND);
    }

    const mbedtls_cipher_info_t *info = mbedtls_cipher_info_from_values(
        MBEDTLS_CIPHER_ID_AES,
        keybits,
        MBEDTLS_MODE_CBC
    );

    struct mbedtls_cipher_context_t *ctx
        = TRY_ALLOC(struct mbedtls_cipher_context_t);
    mbedtls_cipher_init(ctx);

    REBVAL *error = nullptr;

    IF_NOT_0(cleanup, error, mbedtls_cipher_setup(ctx, info));

    IF_NOT_0(cleanup, error, mbedtls_cipher_setkey(
        ctx,
        p_key,
        keybits,
        REF(decrypt) ? MBEDTLS_DECRYPT : MBEDTLS_ENCRYPT
    ));
    rebFree(p_key);

    // Default padding mode is PKCS7, but TLS won't work unless you use zeros.
    // (Shown also by the %ssl_tls.c file for mbedTLS, see AES CBC ciphers.)
    //
    IF_NOT_0(cleanup, error,
        mbedtls_cipher_set_padding_mode(ctx, MBEDTLS_PADDING_NONE)
    );

  blockscope {
    size_t blocksize = mbedtls_cipher_get_block_size(ctx);
    if (rebDid("binary?", ARG(iv), rebEND)) {
        REBSIZ iv_size;
        REBYTE *iv = rebBytes(&iv_size, ARG(iv), rebEND);

        if (iv_size != blocksize) {
            error = rebValue("make error! [",
                "Initialization vector block size not", rebI(blocksize),
            "]", rebEND);
            goto cleanup;
        }

        IF_NOT_0(cleanup, error, mbedtls_cipher_set_iv(ctx, iv, blocksize));
        rebFree(iv);
    }
    else
        assert(rebDid("blank?", ARG(iv), rebEND));
  }

  cleanup:
    if (error) {
        mbedtls_cipher_free(ctx);
        rebJumps ("fail", error, rebEND);
    }

    return Init_Handle_Cdata_Managed(
        D_OUT,
        ctx,
        sizeof(struct mbedtls_cipher_context_t),
        &cleanup_aes_ctx
    );
}


//
//  export aes-stream: native [
//
//  "Encrypt/decrypt data using AES algorithm."
//
//      return: "Encrypted/decrypted data (null if zero length)"
//          [<opt> binary!]
//      ctx "Stream cipher context"
//          [handle!]
//      data [binary!]
//  ]
//
REBNATIVE(aes_stream)
{
    CRYPT_INCLUDE_PARAMS_OF_AES_STREAM;

    if (VAL_HANDLE_CLEANER(ARG(ctx)) != cleanup_aes_ctx)
        rebJumps(
            "fail [{Not a AES context:}", ARG(ctx), "]", rebEND
        );

    struct mbedtls_cipher_context_t *ctx
        = VAL_HANDLE_POINTER(struct mbedtls_cipher_context_t, ARG(ctx));

    REBSIZ ilen;
    REBYTE *input = rebBytes(&ilen, ARG(data), rebEND);

    if (ilen == 0) {
        rebFree(input);
        return nullptr;  // !!! Is NULL a good result for 0 data?
    }

    REBVAL *error = nullptr;
    REBVAL *result = nullptr;

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
    REBSIZ pad_len = (((ilen - 1) >> 4) << 4) + blocksize;

    REBYTE *pad_data;
    if (ilen < pad_len) {
        //
        //  make new data input with zero-padding
        //
        pad_data = rebAllocN(REBYTE, pad_len);
        memset(pad_data, 0, pad_len);
        memcpy(pad_data, input, ilen);
        input = pad_data;
    }
    else
        pad_data = nullptr;

    REBYTE *output = rebAllocN(REBYTE, ilen + blocksize);

    size_t olen;
    IF_NOT_0(cleanup, error,
        mbedtls_cipher_update(ctx, input, pad_len, output, &olen)
    );

    rebFree(input);

    result = rebRepossess(output, olen);

  cleanup:
    if (pad_data)
        rebFree(pad_data);

    if (error)
        rebJumps ("fail", error, rebEND);

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


static const struct mbedtls_ecp_curve_info *Ecp_Curve_Info_From_Word(
    const REBVAL *word
){
    const struct mbedtls_ecp_curve_info *info;

    if (rebDidQ("'curve25519 =", word, rebEND))
        info = &curve25519_info;
    else {
        char *name = rebSpellQ("lowercase to text!", word, rebEND);
        info = mbedtls_ecp_curve_info_from_name(name);
        rebFree(name);
    }

    if (not info)
        rebJumpsQ (
            "fail [{Unknown ECC curve specified:}", word, "]",
        rebEND);

    return info;
}


//
//  export ecc-generate-keypair: native [
//      {Generates an uncompressed secp256r1 key}
//
//      return: "object with PUBLIC/X, PUBLIC/Y, and PRIVATE key members"
//          [object!]
//      group "Elliptic curve group [CURVE25519 SECP256R1 ...]"
//          [word!]
//  ]
//
REBNATIVE(ecc_generate_keypair)
//
// !!! Note: using curve25519 seems to always give a y coordinate of zero
// in the public key.  Is this correct (it seems to yield the right secret)?
{
    CRYPT_INCLUDE_PARAMS_OF_ECC_GENERATE_KEYPAIR;

    // A change in mbedTLS ecdh code means there's a context variable inside
    // the context (ctx.ctx) when not using MBEDTLS_ECDH_LEGACY_CONTEXT
    //
    struct mbedtls_ecdh_context ctx;
    mbedtls_ecdh_init(&ctx);

    REBVAL *error = nullptr;
    REBVAL *result = nullptr;

    const struct mbedtls_ecp_curve_info *info
        = Ecp_Curve_Info_From_Word(ARG(group));
    size_t num_bytes = info->bit_size / 8;

    IF_NOT_0(cleanup, error,
        mbedtls_ecdh_setup(&ctx, info->grp_id)
    );

    IF_NOT_0(cleanup, error, mbedtls_ecdh_gen_public(
        &ctx.ctx.mbed_ecdh.grp,
        &ctx.ctx.mbed_ecdh.d,  // private key
        &ctx.ctx.mbed_ecdh.Q,  // public key (X, Y)
        &get_random,  // f_rng, random number generator
        nullptr  // p_rng, parameter tunneled to random generator (unused atm)
    ));

    // Allocate into memory that can be retaken directly as BINARY! in Rebol
    //
  blockscope {
    uint8_t *p_publicX = rebAllocN(uint8_t, num_bytes);
    uint8_t *p_publicY = rebAllocN(uint8_t, num_bytes);
    uint8_t *p_privateKey = rebAllocN(uint8_t, num_bytes);

    mbedtls_mpi_write_binary(&ctx.ctx.mbed_ecdh.Q.X, p_publicX, num_bytes);
    mbedtls_mpi_write_binary(&ctx.ctx.mbed_ecdh.Q.Y, p_publicY, num_bytes);
    mbedtls_mpi_write_binary(&ctx.ctx.mbed_ecdh.d, p_privateKey, num_bytes);

    result = rebValue(
        "make object! [",
            "public-key: make object! [",
                "x:", rebR(rebRepossess(p_publicX, num_bytes)),
                "y:", rebR(rebRepossess(p_publicY, num_bytes)),
            "]",
            "private-key:", rebR(rebRepossess(p_privateKey, num_bytes)),
        "]",
    rebEND);
  }

  cleanup:
    mbedtls_ecdh_free(&ctx);
    if (error)
        rebJumps ("fail", error, rebEND);

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
REBNATIVE(ecdh_shared_secret)
{
    CRYPT_INCLUDE_PARAMS_OF_ECDH_SHARED_SECRET;

    const struct mbedtls_ecp_curve_info *info
        = Ecp_Curve_Info_From_Word(ARG(group));
    size_t num_bytes = info->bit_size / 8;

    unsigned char *public_key = rebAllocN(REBYTE, num_bytes * 2);

    rebBytesInto(public_key, num_bytes * 2, "use [bin] [",
        "bin: either binary?", ARG(public), "[", ARG(public), "] [",
            "append copy pick", ARG(public), "'x", "pick", ARG(public), "'y"
        "]",
        "if", rebI(num_bytes * 2), "!= length of bin [",
            "fail [{Public BINARY! must be}", rebI(num_bytes * 2),
                "{bytes total for}", rebQ(ARG(group)), "]",
        "]",
        "bin",
    "]", rebEND);

    // A change in mbedTLS ecdh code means there's a context variable inside
    // the context (ctx.ctx) when not using MBEDTLS_ECDH_LEGACY_CONTEXT
    //
    struct mbedtls_ecdh_context ctx;
    mbedtls_ecdh_init(&ctx);

    REBVAL *result = nullptr;
    REBVAL *error = nullptr;

    IF_NOT_0(cleanup, error,
        mbedtls_ecdh_setup(&ctx, info->grp_id)
    );

    IF_NOT_0(cleanup, error, mbedtls_mpi_read_binary(
        &ctx.ctx.mbed_ecdh.Qp.X,
        public_key,
        num_bytes
    ));
    IF_NOT_0(cleanup, error, mbedtls_mpi_read_binary(
        &ctx.ctx.mbed_ecdh.Qp.Y,
        public_key + num_bytes,
        num_bytes
    ));
    IF_NOT_0(cleanup, error, mbedtls_mpi_lset( &ctx.ctx.mbed_ecdh.Qp.Z, 1 ));

    rebElide(
        "if", rebI(num_bytes), "!= length of", ARG(private), "[",
            "fail [{Size of PRIVATE key must be}",
                rebI(num_bytes), "{for}", rebQ(ARG(group)), "]"
        "]",
        ARG(private),
    rebEND);

    IF_NOT_0(cleanup, error,
        Mpi_From_Binary(&ctx.ctx.mbed_ecdh.d, ARG(private)));

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
        rebJumps ("fail", error, rebEND);

    return result;
}


//
//  init-crypto: native [
//
//  {Initialize random number generators and OS-provided crypto services}
//
//      return: [void!]
//  ]
//
REBNATIVE(init_crypto)
{
    CRYPT_INCLUDE_PARAMS_OF_INIT_CRYPTO;

  #ifdef TO_WINDOWS
    if (CryptAcquireContextW(
        &gCryptProv,
        0,
        0,
        PROV_RSA_FULL,
        CRYPT_VERIFYCONTEXT | CRYPT_SILENT
    )){
        return rebVoid();
    }
    gCryptProv = 0;
  #else
    rng_fd = open("/dev/urandom", O_RDONLY);
    if (rng_fd != -1)
        return rebVoid();
  #endif

    // !!! Should we fail here, or wait to fail until the system tries to
    // generate random data and cannot?
    //
    fail ("INIT-CRYPTO couldn't initialize random number generation");
}


//
//  shutdown-crypto: native [
//
//  {Shut down random number generators and OS-provided crypto services}
//
//      return: [void!]
//  ]
//
REBNATIVE(shutdown_crypto)
{
    CRYPT_INCLUDE_PARAMS_OF_SHUTDOWN_CRYPTO;

  #ifdef TO_WINDOWS
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

    return Init_Void(D_OUT, SYM_VOID);
}
