//
//  File: %mod-crypt.c
//  Summary: "Native Functions for cryptography"
//  Section: Extension
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//=////////////////////////////////////////////////////////////////////////=//
//
// The original cryptography additions to Rebol were done by Saphirion, at
// a time prior to Rebol's open sourcing.  They had to go through a brittle,
// incomplete, and difficult to read API for extending the interpreter with
// C code.  This was in a file called %host-core.c.
//
// As a transitional phase, the routines from that file were changed to
// directly use the internal API--the same one used by natives exposed from
// %sys-core.  The longstanding (but not standard, and not particularly
// secure) ENCLOAK and DECLOAK operations from R3-Alpha were moved here too.
//
// That made it easier to see what the code was doing, but the ultimate goal
// is to retarget it to use the new "libRebol" API.  So dependencies on the
// internal API are being slowly cut, as that functionality improves.
//

#include "rc4/rc4.h"
#include "rsa/rsa.h" // defines gCryptProv and rng_fd (used in Init/Shutdown)
#include "dh/dh.h"
#include "aes/aes.h"

#ifdef IS_ERROR
#undef IS_ERROR //winerror.h defines this, so undef it to avoid the warning
#endif
#include "sys-core.h"

#include "sha256/sha256.h" // depends on %reb-c.h for u8, u32, u64

#include "tmp-mod-crypt.h"

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
    if (!CryptAcquireContextW(
        &gCryptProv, 0, 0, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT | CRYPT_SILENT
    )) {
        // !!! There is no good way to return failure here as the
        // routine is designed, and it appears that in some cases
        // a zero initialization worked in the past.  Assert in the
        // debug build but continue silently otherwise.
        assert(false);
        gCryptProv = 0;
    }
  #else
    rng_fd = open("/dev/urandom", O_RDONLY);
    if (rng_fd == -1) {
        // We don't crash the release client now, but we will later
        // if they try to generate random numbers
        assert(false);
    }
  #endif

    return Init_Void(D_OUT);
}


//
//  shutdown-crypto: native [
//
//  {Shut down random number generators and OS-provided crypto services}
//
//  ]
//
REBNATIVE(shutdown_crypto)
{
    CRYPT_INCLUDE_PARAMS_OF_SHUTDOWN_CRYPTO;

  #ifdef TO_WINDOWS
    if (gCryptProv != 0)
        CryptReleaseContext(gCryptProv, 0);
  #else
    if (rng_fd != -1)
        close(rng_fd);
  #endif

    return Init_Void(D_OUT);
}


static void cleanup_rc4_ctx(const REBVAL *v)
{
    RC4_CTX *rc4_ctx = VAL_HANDLE_POINTER(RC4_CTX, v);
    FREE(RC4_CTX, rc4_ctx);
}


//
//  export rc4: native [
//
//  "Encrypt/decrypt data (modifies) using RC4 algorithm."
//
//      return: [handle! logic!]
//          "Returns stream cipher context handle."
//      /key
//          "Provided only for the first time to get stream HANDLE!"
//      crypt-key [binary!]
//          "Crypt key."
//      /stream
//      ctx [handle!]
//          "Stream cipher context."
//      data [binary!]
//          "Data to encrypt/decrypt."
//  ]
//
REBNATIVE(rc4)
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
    CRYPT_INCLUDE_PARAMS_OF_RC4;

    if (REF(stream)) {
        REBVAL *data = ARG(data);

        if (VAL_HANDLE_CLEANER(ARG(ctx)) != cleanup_rc4_ctx)
            rebJumps("fail [{Not a RC4 Context:}", ARG(ctx), "]", rebEND);

        RC4_CTX *rc4_ctx = VAL_HANDLE_POINTER(RC4_CTX, ARG(ctx));

        RC4_crypt(
            rc4_ctx,
            VAL_BIN_AT(data), // input "message"
            VAL_BIN_AT(data), // output (same, since it modifies)
            VAL_LEN_AT(data)
        );

        // In %host-core.c this used to fall through to return the first arg,
        // a refinement, which was true in this case.  :-/
        //
        return rebLogic(true);
    }

    if (REF(key)) { // Key defined - setup new context
        RC4_CTX *rc4_ctx = ALLOC_ZEROFILL(RC4_CTX);

        RC4_setup(
            rc4_ctx,
            VAL_BIN_AT(ARG(crypt_key)),
            VAL_LEN_AT(ARG(crypt_key))
        );

        return Init_Handle_Managed(D_OUT, rc4_ctx, 0, &cleanup_rc4_ctx);
    }

    rebJumps("fail {Refinement /key or /stream has to be present}", rebEND);
}


//
//  export rsa: native [
//
//  "Encrypt/decrypt data using the RSA algorithm."
//
//      data [binary!]
//      key-object [object!]
//      /decrypt
//         "Decrypts the data (default is to encrypt)"
//      /private
//         "Uses an RSA private key (default is a public key)"
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

    RSA_CTX *rsa_ctx = NULL;

    REBINT binary_len;
    if (REF(private)) {
        REBVAL *d = rebValue("ensure binary! pick", obj, "'d", rebEND);

        if (not d)
            fail ("No d returned BLANK, can we assume error for cleanup?");

        REBVAL *p = rebValue("ensure binary! pick", obj, "'p", rebEND);
        REBVAL *q = rebValue("ensure binary! pick", obj, "'q", rebEND);
        REBVAL *dp = rebValue("ensure binary! pick", obj, "'dp", rebEND);
        REBVAL *dq = rebValue("ensure binary! pick", obj, "'dq", rebEND);
        REBVAL *qinv = rebValue("ensure binary! pick", obj, "'qinv", rebEND);

        // !!! Because BINARY! is not locked in memory or safe from GC, the
        // libRebol API doesn't allow direct pointer access.  Use the
        // internal VAL_BIN_AT for now, but consider if a temporary locking
        // should be possible...locked until released.
        //
        binary_len = rebUnbox("length of", d, rebEND);
        RSA_priv_key_new(
            &rsa_ctx
            ,
            VAL_BIN_AT(n)
            , rebUnbox("length of", n, rebEND)
            ,
            VAL_BIN_AT(e)
            , rebUnbox("length of", e, rebEND)
            ,
            VAL_BIN_AT(d)
            , binary_len // taken as `length of d` above
            ,
            p ? VAL_BIN_AT(p) : NULL
            , p ? rebUnbox("length of", p, rebEND) : 0
            ,
            q ? VAL_BIN_AT(q) : NULL
            , q ? rebUnbox("length of", q, rebEND) : 0
            ,
            dp ? VAL_BIN_AT(dp) : NULL
            , dp ? rebUnbox("length of", dp, rebEND) : 0
            ,
            dq ? VAL_BIN_AT(dq) : NULL
            , dp ? rebUnbox("length of", dq, rebEND) : 0
            ,
            qinv ? VAL_BIN_AT(qinv) : NULL
            , qinv ? rebUnbox("length of", qinv, rebEND) : 0
        );

        rebRelease(d);
        rebRelease(p);
        rebRelease(q);
        rebRelease(dp);
        rebRelease(dq);
        rebRelease(qinv);
    }
    else {
        binary_len = rebUnbox("length of", n, rebEND);
        RSA_pub_key_new(
            &rsa_ctx
            ,
            VAL_BIN_AT(n)
            , binary_len // taken as `length of n` above
            ,
            VAL_BIN_AT(e)
            , rebUnbox("length of", e, rebEND)
        );
    }

    rebRelease(n);
    rebRelease(e);

    // !!! See notes above about direct binary access via libRebol
    //
    REBYTE *dataBuffer = VAL_BIN_AT(ARG(data));
    REBINT data_len = rebUnbox("length of", ARG(data), rebEND);

    BI_CTX *bi_ctx = rsa_ctx->bi_ctx;
    bigint *data_bi = bi_import(bi_ctx, dataBuffer, data_len);

    // Buffer suitable for recapturing as a BINARY! for either the encrypted
    // or decrypted data
    //
    REBYTE *crypted = rebAllocN(REBYTE, binary_len);

    if (REF(decrypt)) {
        int result = RSA_decrypt(
            rsa_ctx,
            dataBuffer,
            crypted,
            binary_len,
            REF(private) ? 1 : 0
        );

        if (result == -1) {
            bi_free(rsa_ctx->bi_ctx, data_bi);
            RSA_free(rsa_ctx);

            rebFree(crypted); // would free automatically due to failure...
            rebJumps(
                "fail [{Failed to decrypt:}", ARG(data), "]", rebEND
            );
        }

        assert(result == binary_len); // was this true?
    }
    else {
        int result = RSA_encrypt(
            rsa_ctx,
            dataBuffer,
            data_len,
            crypted,
            REF(private) ? 1 : 0
        );

        if (result == -1) {
            bi_free(rsa_ctx->bi_ctx, data_bi);
            RSA_free(rsa_ctx);

            rebFree(crypted); // would free automatically due to failure...
            rebJumps(
                "fail [{Failed to encrypt:}", ARG(data), "]", rebEND
            );
        }

        // !!! any invariant here?
    }

    bi_free(rsa_ctx->bi_ctx, data_bi);
    RSA_free(rsa_ctx);

    return rebRepossess(crypted, binary_len);
}


//
//  export dh-generate-key: native [
//
//  "Update DH object with new DH private/public key pair."
//
//      return: "No result, object's PRIV-KEY and PUB-KEY members updated"
//          [<opt>]
//      obj [object!]
//         "(modified) Diffie-Hellman object, with generator(g) / modulus(p)"
//  ]
//
REBNATIVE(dh_generate_key)
{
    CRYPT_INCLUDE_PARAMS_OF_DH_GENERATE_KEY;

    DH_CTX dh_ctx;
    memset(&dh_ctx, 0, sizeof(dh_ctx));

    REBVAL *obj = ARG(obj);

    // !!! This used to ensure that all other fields, besides SELF, were blank
    //
    REBVAL *g = rebValue("ensure binary! pick", obj, "'g", rebEND); // generator
    REBVAL *p = rebValue("ensure binary! pick", obj, "'p", rebEND); // modulus

    dh_ctx.g = VAL_BIN_AT(g);
    dh_ctx.glen = rebUnbox("length of", g, rebEND);

    dh_ctx.p = VAL_BIN_AT(p);
    dh_ctx.len = rebUnbox("length of", p, rebEND);

    // Generate the private and public keys into memory that can be
    // rebRepossess()'d as the memory backing a BINARY! series
    //
    dh_ctx.x = rebAllocN(REBYTE, dh_ctx.len); // x => private key
    memset(dh_ctx.x, 0, dh_ctx.len);
    dh_ctx.gx = rebAllocN(REBYTE, dh_ctx.len); // gx => public key
    memset(dh_ctx.gx, 0, dh_ctx.len);

    DH_generate_key(&dh_ctx);

    rebRelease(g);
    rebRelease(p);

    REBVAL *priv = rebRepossess(dh_ctx.x, dh_ctx.len);
    REBVAL *pub = rebRepossess(dh_ctx.gx, dh_ctx.len);

    rebElide("poke", obj, "'priv-key", priv, rebEND);
    rebElide("poke", obj, "'pub-key", pub, rebEND);

    rebRelease(priv);
    rebRelease(pub);

    return nullptr; // !!! Should be void, how to denote?
}


//
//  export dh-compute-key: native [
//
//  "Computes key from a private/public key pair and the peer's public key."
//
//      return: [binary!]
//          "Negotiated key"
//      obj [object!]
//          "The Diffie-Hellman key object"
//      public-key [binary!]
//          "Peer's public key"
//  ]
//
REBNATIVE(dh_compute_key)
{
    CRYPT_INCLUDE_PARAMS_OF_DH_COMPUTE_KEY;

    DH_CTX dh_ctx;
    memset(&dh_ctx, 0, sizeof(dh_ctx));

    REBVAL *obj = ARG(obj);

    // !!! used to ensure object only had other fields SELF, PUB-KEY, G
    // otherwise gave Error(RE_EXT_CRYPT_INVALID_KEY_FIELD)

    REBVAL *p = rebValue("ensure binary! pick", obj, "'p", rebEND);
    REBVAL *priv_key = rebValue("ensure binary! pick", obj, "'priv-key", rebEND);

    dh_ctx.p = VAL_BIN_AT(p);
    dh_ctx.len = rebUnbox("length of", p, rebEND);

    dh_ctx.x = VAL_BIN_AT(priv_key);
    // !!! No length check here, should there be?

    dh_ctx.gy = VAL_BIN_AT(ARG(public_key));
    // !!! No length check here, should there be?

    dh_ctx.k = rebAllocN(REBYTE, dh_ctx.len);
    memset(dh_ctx.k, 0, dh_ctx.len);

    DH_compute_key(&dh_ctx);

    rebRelease(p);
    rebRelease(priv_key);

    return rebRepossess(dh_ctx.k, dh_ctx.len);
}


static void cleanup_aes_ctx(const REBVAL *v)
{
    AES_CTX *aes_ctx = VAL_HANDLE_POINTER(AES_CTX, v);
    FREE(AES_CTX, aes_ctx);
}


//
//  export aes: native [
//
//  "Encrypt/decrypt data using AES algorithm."
//
//      return: [handle! binary! logic!]
//          "Stream cipher context handle or encrypted/decrypted data."
//      /key
//          "Provided only for the first time to get stream HANDLE!"
//      crypt-key [binary!]
//          "Crypt key."
//      iv [binary! blank!]
//          "Optional initialization vector."
//      /stream
//      ctx [handle!]
//          "Stream cipher context."
//      data [binary!]
//          "Data to encrypt/decrypt."
//      /decrypt
//          "Use the crypt-key for decryption (default is to encrypt)"
//  ]
//
REBNATIVE(aes)
{
    CRYPT_INCLUDE_PARAMS_OF_AES;

    if (REF(stream)) {
        if (VAL_HANDLE_CLEANER(ARG(ctx)) != cleanup_aes_ctx)
            rebJumps(
                "fail [{Not a AES context:}", ARG(ctx), "]", rebEND
            );

        AES_CTX *aes_ctx = VAL_HANDLE_POINTER(AES_CTX, ARG(ctx));

        REBYTE *dataBuffer = VAL_BIN_AT(ARG(data));
        REBINT len = VAL_LEN_AT(ARG(data));

        if (len == 0)
            return nullptr; // !!! Is NULL a good result for 0 data?

        REBINT pad_len = (((len - 1) >> 4) << 4) + AES_BLOCKSIZE;

        REBYTE *pad_data;
        if (len < pad_len) {
            //
            //  make new data input with zero-padding
            //
            pad_data = rebAllocN(REBYTE, pad_len);
            memset(pad_data, 0, pad_len);
            memcpy(pad_data, dataBuffer, len);
            dataBuffer = pad_data;
        }
        else
            pad_data = nullptr;

        REBYTE *data_out = rebAllocN(REBYTE, pad_len);
        memset(data_out, 0, pad_len);

        if (aes_ctx->key_mode == AES_MODE_DECRYPT)
            AES_cbc_decrypt(
                aes_ctx,
                cast(const uint8_t*, dataBuffer),
                data_out,
                pad_len
            );
        else
            AES_cbc_encrypt(
                aes_ctx,
                cast(const uint8_t*, dataBuffer),
                data_out,
                pad_len
            );

        if (pad_data)
            rebFree(pad_data);

        return rebRepossess(data_out, pad_len);
    }

    if (REF(key)) {
        uint8_t iv[AES_IV_SIZE];

        if (IS_BINARY(ARG(iv))) {
            if (VAL_LEN_AT(ARG(iv)) < AES_IV_SIZE)
                fail ("Length of initialization vector less than AES size");

            memcpy(iv, VAL_BIN_AT(ARG(iv)), AES_IV_SIZE);
        }
        else {
            assert(IS_BLANK(ARG(iv)));
            memset(iv, 0, AES_IV_SIZE);
        }

        //key defined - setup new context

        REBINT len = VAL_LEN_AT(ARG(crypt_key)) << 3;
        if (len != 128 and len != 256) {
            DECLARE_LOCAL (i);
            Init_Integer(i, len);
            rebJumps(
                "fail [{AES key length has to be 16 or 32, not:}",
                    rebI(len), "]", rebEND
            );
        }

        AES_CTX *aes_ctx = ALLOC_ZEROFILL(AES_CTX);

        AES_set_key(
            aes_ctx,
            cast(const uint8_t *, VAL_BIN_AT(ARG(crypt_key))),
            cast(const uint8_t *, iv),
            (len == 128) ? AES_MODE_128 : AES_MODE_256
        );

        if (REF(decrypt))
            AES_convert_key(aes_ctx);

        return Init_Handle_Managed(D_OUT, aes_ctx, 0, &cleanup_aes_ctx);
    }

    rebJumps("fail {Refinement /key or /stream has to be present}", rebEND);
}


//
//  export sha256: native [
//
//  {Calculate a SHA256 hash value from binary data.}
//
//      return: [binary!]
//          {32-byte binary hash}
//      data [binary! text!]
//          {Data to hash, TEXT! will be converted to UTF-8}
//  ]
//
REBNATIVE(sha256)
{
    CRYPT_INCLUDE_PARAMS_OF_SHA256;

    REBVAL *data = ARG(data);

    REBYTE *bp;
    REBSIZ size;
    if (IS_TEXT(data)) {
        REBSIZ offset;
        REBSER *temp = Temp_UTF8_At_Managed(
            &offset, &size, data, VAL_LEN_AT(data)
        );
        bp = BIN_AT(temp, offset);
    }
    else {
        assert(IS_BINARY(data));

        bp = VAL_BIN_AT(data);
        size = VAL_LEN_AT(data);
    }

    SHA256_CTX ctx;

    sha256_init(&ctx);
    sha256_update(&ctx, bp, size);

    REBYTE *buf = rebAllocN(REBYTE, SHA256_BLOCK_SIZE);
    sha256_final(&ctx, buf);
    return rebRepossess(buf, SHA256_BLOCK_SIZE);
}
