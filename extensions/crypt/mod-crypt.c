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

// !!! Modern Ren-C's crypt module does not use %sys-core.h, it uses %rebol.h,
// so these conflicts don't happen.
//
#ifdef OUT
    #undef OUT  // %minwindef.h defines this, we have a better use for it
#endif
#ifdef VOID
    #undef VOID  // %winnt.h defines this, we have a better use for it
#endif

#include "sys-core.h"

#include "sha256/sha256.h"

#include "tmp-mod-crypt.h"

//
//  init-crypto: native [
//
//  {Initialize random number generators and OS-provided crypto services}
//
//      return: [nothing!]
//  ]
//
DECLARE_NATIVE(INIT_CRYPTO)
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

    return Init_Nothing(OUT);
}


//
//  shutdown-crypto: native [
//
//  {Shut down random number generators and OS-provided crypto services}
//
//  ]
//
DECLARE_NATIVE(SHUTDOWN_CRYPTO)
{
    CRYPT_INCLUDE_PARAMS_OF_SHUTDOWN_CRYPTO;

  #ifdef TO_WINDOWS
    if (gCryptProv != 0)
        CryptReleaseContext(gCryptProv, 0);
  #else
    if (rng_fd != -1)
        close(rng_fd);
  #endif

    return Init_Nothing(OUT);
}


static void cleanup_rc4_ctx(const Value* v)
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
DECLARE_NATIVE(RC4)
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

    if (Bool_ARG(STREAM)) {
        Value* data = ARG(DATA);

        if (VAL_HANDLE_CLEANER(ARG(CTX)) != cleanup_rc4_ctx)
            rebJumps("fail [{Not a RC4 Context:}", ARG(CTX), "]");

        RC4_CTX *rc4_ctx = VAL_HANDLE_POINTER(RC4_CTX, ARG(CTX));

        RC4_crypt(
            rc4_ctx,
            Cell_Blob_At(data), // input "message"
            Cell_Blob_At(data), // output (same, since it modifies)
            Cell_Series_Len_At(data)
        );

        // In %host-core.c this used to fall through to return the first arg,
        // a refinement, which was true in this case.  :-/
        //
        return rebLogic(true);
    }

    if (Bool_ARG(KEY)) { // Key defined - setup new context
        RC4_CTX *rc4_ctx = ALLOC_ZEROFILL(RC4_CTX);

        RC4_setup(
            rc4_ctx,
            Cell_Blob_At(ARG(CRYPT_KEY)),
            Cell_Series_Len_At(ARG(CRYPT_KEY))
        );

        return Init_Handle_Managed(OUT, rc4_ctx, 0, &cleanup_rc4_ctx);
    }

    rebJumps("fail {Refinement /key or /stream has to be present}");
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
DECLARE_NATIVE(RSA)
{
    CRYPT_INCLUDE_PARAMS_OF_RSA;

    Value* obj = ARG(KEY_OBJECT);

    // N and E are required
    //
    Value* n = rebValue("ensure binary! pick", obj, "'n");
    Value* e = rebValue("ensure binary! pick", obj, "'e");

    RSA_CTX *rsa_ctx = nullptr;

    REBINT binary_len;
    if (Bool_ARG(PRIVATE)) {
        Value* d = rebValue("ensure binary! pick", obj, "'d");

        if (not d)
            fail ("No d returned BLANK, can we assume error for cleanup?");

        Value* p = rebValue("ensure binary! pick", obj, "'p");
        Value* q = rebValue("ensure binary! pick", obj, "'q");
        Value* dp = rebValue("ensure binary! pick", obj, "'dp");
        Value* dq = rebValue("ensure binary! pick", obj, "'dq");
        Value* qinv = rebValue("ensure binary! pick", obj, "'qinv");

        // !!! Because BINARY! is not locked in memory or safe from GC, the
        // libRebol API doesn't allow direct pointer access.  Use the
        // internal VAL_BIN_AT for now, but consider if a temporary locking
        // should be possible...locked until released.
        //
        binary_len = rebUnbox("length of", d);
        RSA_priv_key_new(
            &rsa_ctx
            ,
            Cell_Blob_At(n)
            , rebUnbox("length of", n)
            ,
            Cell_Blob_At(e)
            , rebUnbox("length of", e)
            ,
            Cell_Blob_At(d)
            , binary_len // taken as `length of d` above
            ,
            p ? Cell_Blob_At(p) : nullptr
            , p ? rebUnbox("length of", p) : 0
            ,
            q ? Cell_Blob_At(q) : nullptr
            , q ? rebUnbox("length of", q) : 0
            ,
            dp ? Cell_Blob_At(dp) : nullptr
            , dp ? rebUnbox("length of", dp) : 0
            ,
            dq ? Cell_Blob_At(dq) : nullptr
            , dp ? rebUnbox("length of", dq) : 0
            ,
            qinv ? Cell_Blob_At(qinv) : nullptr
            , qinv ? rebUnbox("length of", qinv) : 0
        );

        rebRelease(d);
        rebRelease(p);
        rebRelease(q);
        rebRelease(dp);
        rebRelease(dq);
        rebRelease(qinv);
    }
    else {
        binary_len = rebUnbox("length of", n);
        RSA_pub_key_new(
            &rsa_ctx
            ,
            Cell_Blob_At(n)
            , binary_len // taken as `length of n` above
            ,
            Cell_Blob_At(e)
            , rebUnbox("length of", e)
        );
    }

    rebRelease(n);
    rebRelease(e);

    // !!! See notes above about direct binary access via libRebol
    //
    Byte *dataBuffer = Cell_Blob_At(ARG(DATA));
    REBINT data_len = rebUnbox("length of", ARG(DATA));

    BI_CTX *bi_ctx = rsa_ctx->bi_ctx;
    bigint *data_bi = bi_import(bi_ctx, dataBuffer, data_len);

    // Buffer suitable for recapturing as a BINARY! for either the encrypted
    // or decrypted data
    //
    Byte *crypted = rebAllocN(Byte, binary_len);

    if (Bool_ARG(DECRYPT)) {
        int result = RSA_decrypt(
            rsa_ctx,
            dataBuffer,
            crypted,
            binary_len,
            Bool_ARG(PRIVATE) ? 1 : 0
        );

        if (result == -1) {
            bi_free(rsa_ctx->bi_ctx, data_bi);
            RSA_free(rsa_ctx);

            rebFree(crypted); // would free automatically due to failure...
            rebJumps(
                "fail [{Failed to decrypt:}", ARG(DATA), "]"
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
            Bool_ARG(PRIVATE) ? 1 : 0
        );

        if (result == -1) {
            bi_free(rsa_ctx->bi_ctx, data_bi);
            RSA_free(rsa_ctx);

            rebFree(crypted); // would free automatically due to failure...
            rebJumps(
                "fail [{Failed to encrypt:}", ARG(DATA), "]"
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
//          [~null~]
//      obj [object!]
//         "(modified) Diffie-Hellman object, with generator(g) / modulus(p)"
//  ]
//
DECLARE_NATIVE(DH_GENERATE_KEY)
{
    CRYPT_INCLUDE_PARAMS_OF_DH_GENERATE_KEY;

    DH_CTX dh_ctx;
    memset(&dh_ctx, 0, sizeof(dh_ctx));

    Value* obj = ARG(OBJ);

    // !!! This used to ensure that all other fields, besides SELF, were blank
    //
    Value* g = rebValue("ensure binary! pick", obj, "'g"); // generator
    Value* p = rebValue("ensure binary! pick", obj, "'p"); // modulus

    dh_ctx.g = Cell_Blob_At(g);
    dh_ctx.glen = rebUnbox("length of", g);

    dh_ctx.p = Cell_Blob_At(p);
    dh_ctx.len = rebUnbox("length of", p);

    // Generate the private and public keys into memory that can be
    // rebRepossess()'d as the memory backing a BINARY! series
    //
    dh_ctx.x = rebAllocN(Byte, dh_ctx.len); // x => private key
    memset(dh_ctx.x, 0, dh_ctx.len);
    dh_ctx.gx = rebAllocN(Byte, dh_ctx.len); // gx => public key
    memset(dh_ctx.gx, 0, dh_ctx.len);

    DH_generate_key(&dh_ctx);

    rebRelease(g);
    rebRelease(p);

    Value* priv = rebRepossess(dh_ctx.x, dh_ctx.len);
    Value* pub = rebRepossess(dh_ctx.gx, dh_ctx.len);

    rebElide("poke", obj, "'priv-key", priv);
    rebElide("poke", obj, "'pub-key", pub);

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
DECLARE_NATIVE(DH_COMPUTE_KEY)
{
    CRYPT_INCLUDE_PARAMS_OF_DH_COMPUTE_KEY;

    DH_CTX dh_ctx;
    memset(&dh_ctx, 0, sizeof(dh_ctx));

    Value* obj = ARG(OBJ);

    // !!! used to ensure object only had other fields SELF, PUB-KEY, G
    // otherwise gave Error(RE_EXT_CRYPT_INVALID_KEY_FIELD)

    Value* p = rebValue("ensure binary! pick", obj, "'p");
    Value* priv_key = rebValue("ensure binary! pick", obj, "'priv-key");

    dh_ctx.p = Cell_Blob_At(p);
    dh_ctx.len = rebUnbox("length of", p);

    dh_ctx.x = Cell_Blob_At(priv_key);
    // !!! No length check here, should there be?

    dh_ctx.gy = Cell_Blob_At(ARG(PUBLIC_KEY));
    // !!! No length check here, should there be?

    dh_ctx.k = rebAllocN(Byte, dh_ctx.len);
    memset(dh_ctx.k, 0, dh_ctx.len);

    DH_compute_key(&dh_ctx);

    rebRelease(p);
    rebRelease(priv_key);

    return rebRepossess(dh_ctx.k, dh_ctx.len);
}


static void cleanup_aes_ctx(const Value* v)
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
DECLARE_NATIVE(AES)
{
    CRYPT_INCLUDE_PARAMS_OF_AES;

    if (Bool_ARG(STREAM)) {
        if (VAL_HANDLE_CLEANER(ARG(CTX)) != cleanup_aes_ctx)
            rebJumps(
                "fail [{Not a AES context:}", ARG(CTX), "]"
            );

        AES_CTX *aes_ctx = VAL_HANDLE_POINTER(AES_CTX, ARG(CTX));

        Byte *dataBuffer = Cell_Blob_At(ARG(DATA));
        REBINT len = Cell_Series_Len_At(ARG(DATA));

        if (len == 0)
            return nullptr;  // !!! Is nullptr a good result for 0 data?

        REBINT pad_len = (((len - 1) >> 4) << 4) + AES_BLOCKSIZE;

        Byte *pad_data;
        if (len < pad_len) {
            //
            //  make new data input with zero-padding
            //
            pad_data = rebAllocN(Byte, pad_len);
            memset(pad_data, 0, pad_len);
            memcpy(pad_data, dataBuffer, len);
            dataBuffer = pad_data;
        }
        else
            pad_data = nullptr;

        Byte *data_out = rebAllocN(Byte, pad_len);
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

    if (Bool_ARG(KEY)) {
        uint8_t iv[AES_IV_SIZE];

        if (Is_Binary(ARG(IV))) {
            if (Cell_Series_Len_At(ARG(IV)) < AES_IV_SIZE)
                fail ("Length of initialization vector less than AES size");

            memcpy(iv, Cell_Blob_At(ARG(IV)), AES_IV_SIZE);
        }
        else {
            assert(Is_Blank(ARG(IV)));
            memset(iv, 0, AES_IV_SIZE);
        }

        //key defined - setup new context

        REBINT len = Cell_Series_Len_At(ARG(CRYPT_KEY)) << 3;
        if (len != 128 and len != 256) {
            DECLARE_VALUE (i);
            Init_Integer(i, len);
            rebJumps(
                "fail [{AES key length has to be 16 or 32, not:}",
                    rebI(len), "]"
            );
        }

        AES_CTX *aes_ctx = ALLOC_ZEROFILL(AES_CTX);

        AES_set_key(
            aes_ctx,
            cast(const uint8_t *, Cell_Blob_At(ARG(CRYPT_KEY))),
            cast(const uint8_t *, iv),
            (len == 128) ? AES_MODE_128 : AES_MODE_256
        );

        if (Bool_ARG(DECRYPT))
            AES_convert_key(aes_ctx);

        return Init_Handle_Managed(OUT, aes_ctx, 0, &cleanup_aes_ctx);
    }

    rebJumps("fail {Refinement /key or /stream has to be present}");
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
DECLARE_NATIVE(SHA256)
{
    CRYPT_INCLUDE_PARAMS_OF_SHA256;

    Value* data = ARG(DATA);

    Byte *bp;
    Size size;
    if (Is_Text(data)) {
        Size offset;
        Binary* temp = Temp_UTF8_At_Managed(
            &offset, &size, data, Cell_Series_Len_At(data)
        );
        bp = Binary_At(temp, offset);
    }
    else {
        assert(Is_Binary(data));

        bp = Cell_Blob_At(data);
        size = Cell_Series_Len_At(data);
    }

    SHA256_CTX ctx;

    sha256_init(&ctx);
    sha256_update(&ctx, bp, size);

    Byte *buf = rebAllocN(Byte, SHA256_BLOCK_SIZE);
    sha256_final(&ctx, buf);
    return rebRepossess(buf, SHA256_BLOCK_SIZE);
}
