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
// C code.
//
// This contains a simplification of %host-core.c, written directly to the
// native API.  It also includes the longstanding (but not standard, and not
// particularly secure) ENCLOAK and DECLOAK operations from R3-Alpha.
//

#include "rc4/rc4.h"
#include "rsa/rsa.h" // defines gCryptProv and rng_fd (used in Init/Shutdown)
#include "dh/dh.h"
#include "aes/aes.h"

#ifdef IS_ERROR
#undef IS_ERROR //winerror.h defines this, so undef it to avoid the warning
#endif
#include "sys-core.h"
#include "sys-ext.h"

#include "sha256/sha256.h" // depends on Reb-C for REBCNT, REBYTE

#include "tmp-mod-crypt-first.h"

//
//  Init_Crypto: C
//
void Init_Crypto(void)
{
#ifdef TO_WINDOWS
    if (!CryptAcquireContextW(
        &gCryptProv, 0, 0, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT | CRYPT_SILENT
    )) {
        // !!! There is no good way to return failure here as the
        // routine is designed, and it appears that in some cases
        // a zero initialization worked in the past.  Assert in the
        // debug build but continue silently otherwise.
        assert(FALSE);
        gCryptProv = 0;
    }
#else
    rng_fd = open("/dev/urandom", O_RDONLY);
    if (rng_fd == -1) {
        // We don't crash the release client now, but we will later
        // if they try to generate random numbers
        assert(FALSE);
    }
#endif
}


//
//  Shutdown_Crypto: C
//
void Shutdown_Crypto(void)
{
#ifdef TO_WINDOWS
    if (gCryptProv != 0)
        CryptReleaseContext(gCryptProv, 0);
#else
    if (rng_fd != -1)
        close(rng_fd);
#endif
}


static void cleanup_rc4_ctx(const REBVAL *v)
{
    RC4_CTX *rc4_ctx = VAL_HANDLE_POINTER(RC4_CTX, v);
    FREE(RC4_CTX, rc4_ctx);
}


//
//  rc4: native/export [
//
//  "Encrypt/decrypt data (modifies) using RC4 algorithm."
//
//      return: [handle!]
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
//  new-errors: [
//      key-or-stream-required: {Refinement /key or /stream has to be present}
//      invalid-rc4-context: [{Not a RC4 context:} :arg1]
//  ]
//
static REBNATIVE(rc4)
{
    INCLUDE_PARAMS_OF_RC4;

    if (REF(stream)) {
        REBVAL *data = ARG(data);

        if (VAL_HANDLE_CLEANER(ARG(ctx)) != cleanup_rc4_ctx)
            fail (Error(RE_EXT_CRYPT_INVALID_RC4_CONTEXT, ARG(ctx)));

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
        return R_TRUE;
    }

    if (IS_BINARY(ARG(crypt_key))) { // Key defined - setup new context
        RC4_CTX *rc4_ctx = ALLOC_ZEROFILL(RC4_CTX);

        RC4_setup(rc4_ctx, VAL_BIN_AT(ARG(key)), VAL_LEN_AT(ARG(key)));

        Init_Handle_Managed(D_OUT, rc4_ctx, 0, &cleanup_rc4_ctx);
        return R_OUT;
    }

    fail (Error(RE_EXT_CRYPT_KEY_OR_STREAM_REQUIRED));
}


//
//  rsa: native/export [
//
//  "Encrypt/decrypt data using the RSA algorithm."
//
//      data [binary!]
//      key-object [object!]
//      /decrypt
//         "Decrypts the data (default is to encrypt)"
//      /private
//         "Uses an RSA private key (default is a public key)"
//      /padding
//          "Selects the type of padding to use"
//      padding-type [word! blank!]
//          "Type of padding. Available values: PKCS1 or NONE"
//  ]
//  new-words: [n e d p q dp dq qinv pkcs1]
//  new-errors: [
//      invalid-key-field: [{Unrecognized field in the key object:} :arg1]
//      invalid-key-data: [{Invalid data in the key object:} :arg1 {for} :arg2]
//      invalid-key: [{No valid key in the object:} :obj]
//      decryption-failure: [{Failed to decrypt:} :arg1]
//      encryption-failure: [{Failed to encrypt:} :arg1]
//  ]
//
static REBNATIVE(rsa)
{
    INCLUDE_PARAMS_OF_RSA;

    REBOOL padding;
    if (REF(padding))
        padding = NOT(IS_BLANK(ARG(padding_type)));
    else
        padding = TRUE; // PKCS1 is on by default

    REBYTE *n = NULL;
    REBYTE *e = NULL;
    REBYTE *d = NULL;
    REBYTE *p = NULL;
    REBYTE *q = NULL;
    REBYTE *dp = NULL;
    REBYTE *dq = NULL;
    REBYTE *qinv = NULL;

    REBINT n_len = 0;
    REBINT e_len = 0;
    REBINT d_len = 0;
    REBINT p_len = 0;
    REBINT q_len = 0;
    REBINT dp_len = 0;
    REBINT dq_len = 0;
    REBINT qinv_len = 0;

    REBCTX *obj = VAL_CONTEXT(ARG(key_object));

    REBVAL *key = CTX_KEYS_HEAD(obj);
    REBVAL *var = CTX_VARS_HEAD(obj);

    for (; NOT_END(key); ++key, ++var) {
        if (VAL_KEY_SYM(key) == SYM_SELF //object may have a 'self key that referring to itself
            || IS_BLANK(var) //some fields are initialized to blank
           )
            continue;

        if (!IS_BINARY(var))
            fail (Error(RE_EXT_CRYPT_INVALID_KEY_DATA, var, key));

        REBSTR* word = VAL_KEY_CANON(key);
        if (word == CRYPT_WORD_N) {
            n = VAL_BIN_AT(var);
            n_len = VAL_LEN_AT(var);
        }
        else if (word == CRYPT_WORD_E) {
            e = VAL_BIN_AT(var);
            e_len = VAL_LEN_AT(var);
        }
        else if (word == CRYPT_WORD_D) {
            d = VAL_BIN_AT(var);
            d_len = VAL_LEN_AT(var);
        }
        else if (word == CRYPT_WORD_P) {
            p = VAL_BIN_AT(var);
            p_len = VAL_LEN_AT(var);
        }
        else if (word == CRYPT_WORD_Q) {
            q = VAL_BIN_AT(var);
            q_len = VAL_LEN_AT(var);
           break;
        }
        else if (word == CRYPT_WORD_DP) {
            dp = VAL_BIN_AT(var);
            dp_len = VAL_LEN_AT(var);
        }
        else if (word == CRYPT_WORD_DQ) {
            dq = VAL_BIN_AT(var);
            dq_len = VAL_LEN_AT(var);
        }
        else if (word == CRYPT_WORD_QINV) {
            qinv = VAL_BIN_AT(var);
            qinv_len = VAL_LEN_AT(var);
        }
        else {
            fail (Error(RE_EXT_CRYPT_INVALID_KEY_FIELD, key));
        }
    }

    if (!n || !e)
        fail (Error(RE_EXT_CRYPT_INVALID_KEY, ARG(key_object)));

    RSA_CTX *rsa_ctx = NULL;

    REBINT binary_len;
    if (REF(private)) {
        if (!d)
            return R_BLANK;

        RSA_priv_key_new(
            &rsa_ctx, n, n_len, e, e_len, d, d_len,
            p, p_len, q, q_len, dp, dp_len, dq, dq_len, qinv, qinv_len
        );
        binary_len = d_len;
    }
    else {
        RSA_pub_key_new(&rsa_ctx, n, n_len, e, e_len);
        binary_len = n_len;
    }

    REBYTE *dataBuffer = VAL_BIN_AT(ARG(data));
    REBINT data_len = VAL_LEN_AT(ARG(data));

    BI_CTX *bi_ctx = rsa_ctx->bi_ctx;
    bigint *data_bi = bi_import(bi_ctx, dataBuffer, data_len);

    REBSER *binary = Make_Binary(binary_len);

    if (REF(decrypt)) {
        binary_len = RSA_decrypt(
            rsa_ctx,
            dataBuffer,
            BIN_HEAD(binary),
            REF(private) ? 1 : 0,
            padding ? 1 : 0
        );

        if (binary_len == -1) {
            bi_free(rsa_ctx->bi_ctx, data_bi);
            RSA_free(rsa_ctx);

            Free_Series(binary);
            fail (Error(RE_EXT_CRYPT_DECRYPTION_FAILURE, ARG(data)));
        }
    }
    else {
        if (
            -1 == RSA_encrypt(
                rsa_ctx,
                dataBuffer,
                data_len,
                BIN_HEAD(binary),
                REF(private) ? 1 : 0,
                padding ? 1 : 0
            )
        ){
            bi_free(rsa_ctx->bi_ctx, data_bi);
            RSA_free(rsa_ctx);

            Free_Series(binary);
            fail (Error(RE_EXT_CRYPT_ENCRYPTION_FAILURE, ARG(data)));
        }
    }

    SET_SERIES_LEN(binary, binary_len);

    bi_free(rsa_ctx->bi_ctx, data_bi);
    RSA_free(rsa_ctx);

    Init_Binary(D_OUT, binary);
    return R_OUT;
}


//
//  dh-generate-key: native/export [
//
//  "Generates a new DH private/public key pair."
//
//      return: [<opt>]
//      obj [object!]
//         "The Diffie-Hellman key object, with generator(g) and modulus(p)"
//  ]
//  new-words: [priv-key pub-key p g]
//
static REBNATIVE(dh_generate_key)
{
    INCLUDE_PARAMS_OF_DH_GENERATE_KEY;

    DH_CTX dh_ctx;
    memset(&dh_ctx, 0, sizeof(dh_ctx));

    REBCTX *obj = VAL_CONTEXT(ARG(obj));

    REBVAL *key = CTX_KEYS_HEAD(obj);
    REBVAL *var = CTX_VARS_HEAD(obj);

    for (; NOT_END(key); ++key, ++var) {
        if (VAL_KEY_SYM(key) == SYM_SELF //object may have a 'self key that referring to itself
            || IS_BLANK(var) //some fields are initialized to blank
           )
            continue;

        if (!IS_BINARY(var))
            fail (Error(RE_EXT_CRYPT_INVALID_KEY_DATA, var, key));

        REBSTR* word = VAL_KEY_CANON(key);
        if (word == CRYPT_WORD_P) {
            dh_ctx.p = VAL_BIN_AT(var);
            dh_ctx.len = VAL_LEN_AT(var);
            break;
        }
        else if (word == CRYPT_WORD_G) {
            dh_ctx.g = VAL_BIN_AT(var);
            dh_ctx.glen = VAL_LEN_AT(var);
        }
        else {
            fail (Error(RE_EXT_CRYPT_INVALID_KEY_FIELD, key));
        }
    }

    if (!dh_ctx.p || !dh_ctx.g)
        fail (Error(RE_EXT_CRYPT_INVALID_KEY, ARG(obj)));

    // allocate new BINARY! for private key
    //
    REBSER *priv_bin = Make_Binary(dh_ctx.len);
    dh_ctx.x = BIN_HEAD(priv_bin);
    memset(dh_ctx.x, 0, dh_ctx.len);
    SET_SERIES_LEN(priv_bin, dh_ctx.len);

    // allocate new BINARY! for public key
    //
    REBSER *pub_bin = Make_Binary(dh_ctx.len);
    dh_ctx.gx = BIN_HEAD(pub_bin);
    memset(dh_ctx.gx, 0, dh_ctx.len);
    SET_SERIES_LEN(pub_bin, dh_ctx.len);

    DH_generate_key(&dh_ctx);

    // set the object fields

    REBCNT priv_index = Find_Canon_In_Context(obj, CRYPT_WORD_PRIV_KEY, FALSE);
    if (priv_index == 0)
        fail ("Cannot find PRIV-KEY in crypto object");
    Init_Binary(CTX_VAR(obj, priv_index), priv_bin);

    REBCNT pub_index = Find_Canon_In_Context(obj, CRYPT_WORD_PUB_KEY, FALSE);
    if (pub_index == 0)
        fail ("Cannot find PUB-KEY in crypto object");
    Init_Binary(CTX_VAR(obj, pub_index), pub_bin);

    return R_VOID;
}


//
//  dh-compute-key: native/export [
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
static REBNATIVE(dh_compute_key)
{
    INCLUDE_PARAMS_OF_DH_COMPUTE_KEY;

    DH_CTX dh_ctx;
    memset(&dh_ctx, 0, sizeof(dh_ctx));

    REBCTX *obj = VAL_CONTEXT(ARG(obj));

    REBVAL *key = CTX_KEYS_HEAD(obj);
    REBVAL *var = CTX_VARS_HEAD(obj);

    for (; NOT_END(key); ++key, ++var) {
        REBSTR* canon = VAL_KEY_CANON(key);

        if (canon == Canon(SYM_SELF)) {
            NOOP;
        }
        else if (canon == CRYPT_WORD_P) {
            if (NOT(IS_BINARY(var)))
                fail (Error(RE_EXT_CRYPT_INVALID_KEY, var));

            dh_ctx.p = VAL_BIN_AT(var);
            dh_ctx.len = VAL_LEN_AT(var);
        }
        else if (canon == CRYPT_WORD_PRIV_KEY) { 
            if (NOT(IS_BINARY(var)))
                fail (Error(RE_EXT_CRYPT_INVALID_KEY, var));

            dh_ctx.x = VAL_BIN_AT(var);
        }
        else if (canon == CRYPT_WORD_PUB_KEY) {
            NOOP;
        }
        else if (canon == CRYPT_WORD_G) {
            NOOP;
        }
        else
            fail (Error(RE_EXT_CRYPT_INVALID_KEY_FIELD, key));
    }

    dh_ctx.gy = VAL_BIN_AT(ARG(public_key));

    if (!dh_ctx.p || !dh_ctx.x || !dh_ctx.gy)
        fail (Error(RE_EXT_CRYPT_INVALID_KEY, ARG(obj)));

    REBSER *binary = Make_Binary(dh_ctx.len);
    memset(BIN_HEAD(binary), 0, dh_ctx.len);
    SET_SERIES_LEN(binary, dh_ctx.len);

    dh_ctx.k = BIN_HEAD(binary);

    DH_compute_key(&dh_ctx);

    Init_Binary(D_OUT, binary);
    return R_OUT;
}


static void cleanup_aes_ctx(const REBVAL *v)
{
    AES_CTX *aes_ctx = VAL_HANDLE_POINTER(AES_CTX, v);
    FREE(AES_CTX, aes_ctx);
}


//
//  aes: native/export [
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
//  new-errors: [
//      invalid-aes-context: [{Not a AES context:} :arg1]
//      invalid-aes-key-length: [{AES key length has to be 16 or 32:} :arg1]
//  ]
//
static REBNATIVE(aes)
{
    INCLUDE_PARAMS_OF_AES;

    if (REF(stream)) {
        if (VAL_HANDLE_CLEANER(ARG(ctx)) != cleanup_aes_ctx)
            fail (Error(RE_EXT_CRYPT_INVALID_AES_CONTEXT, ARG(ctx)));

        AES_CTX *aes_ctx = VAL_HANDLE_POINTER(AES_CTX, ARG(ctx));

        REBYTE *dataBuffer = VAL_BIN_AT(ARG(data));
        REBINT len = VAL_LEN_AT(ARG(data));

        if (len == 0)
            return R_BLANK;

        REBINT pad_len = (((len - 1) >> 4) << 4) + AES_BLOCKSIZE;

        REBYTE *pad_data;
        if (len < pad_len) {
            //
            //  make new data input with zero-padding
            //
            pad_data = ALLOC_N(REBYTE, pad_len);
            memset(pad_data, 0, pad_len);
            memcpy(pad_data, dataBuffer, len);
            dataBuffer = pad_data;
        }
        else
            pad_data = NULL;

        REBSER *binaryOut = Make_Binary(pad_len);
        memset(BIN_HEAD(binaryOut), 0, pad_len);

        if (aes_ctx->key_mode == AES_MODE_DECRYPT)
            AES_cbc_decrypt(
                aes_ctx,
                cast(const uint8_t*, dataBuffer),
                BIN_HEAD(binaryOut),
                pad_len
            );
        else
            AES_cbc_encrypt(
                aes_ctx,
                cast(const uint8_t*, dataBuffer),
                BIN_HEAD(binaryOut),
                pad_len
            );

        if (pad_data)
            FREE_N(REBYTE, pad_len, pad_data);

        SET_SERIES_LEN(binaryOut, pad_len);
        Init_Binary(D_OUT, binaryOut);
        return R_OUT;
    }

    if (REF(key)) {
        uint8_t iv[AES_IV_SIZE];

        if (IS_BINARY(ARG(iv))) {
            if (VAL_LEN_AT(ARG(iv)) < AES_IV_SIZE)
                return R_BLANK;

            memcpy(iv, VAL_BIN_AT(ARG(iv)), AES_IV_SIZE);
        }
        else {
            assert(IS_BLANK(ARG(iv)));
            memset(iv, 0, AES_IV_SIZE);
        }

        //key defined - setup new context

        REBINT len = VAL_LEN_AT(ARG(crypt_key)) << 3;
        if (len != 128 && len != 256) {
            DECLARE_LOCAL (i);
            Init_Integer(i, len);
            fail (Error(RE_EXT_CRYPT_INVALID_AES_KEY_LENGTH, i));
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

        Init_Handle_Managed(D_OUT, aes_ctx, 0, &cleanup_aes_ctx);
        return R_OUT;
    }

    fail (Error(RE_EXT_CRYPT_KEY_OR_STREAM_REQUIRED));
}


//
//  sha256: native/export [
//
//  {Calculate a SHA256 hash value from binary data.}
//
//      return: [binary!]
//          {32-byte binary hash}
//      data [binary! string!]
//          {Data to hash, STRING! will be converted to UTF-8}
//  ]
//
REBNATIVE(sha256)
{
    INCLUDE_PARAMS_OF_SHA256;

    REBCNT index;
    REBCNT len;
    REBSER *series;
    if (NOT(VAL_BYTE_SIZE(ARG(data)))) { // wide string
        series = Temp_Bin_Str_Managed(ARG(data), &index, &len);
    }
    else {
        series = VAL_SERIES(ARG(data));
        index = VAL_INDEX(ARG(data));
        len = VAL_LEN_AT(ARG(data));
    }

    REBYTE *data = BIN_AT(series, index);

    SHA256_CTX ctx;

    sha256_init(&ctx);
    sha256_update(&ctx, data, len);

    REBSER *buf = Make_Binary(SHA256_BLOCK_SIZE);
    sha256_final(&ctx, BIN_HEAD(buf));
    TERM_BIN_LEN(buf, SHA256_BLOCK_SIZE);

    Init_Binary(D_OUT, buf);
    return R_OUT;
}


/*
#define SEED_LEN 10
static REBYTE seed_str[SEED_LEN] = {
    249, 52, 217, 38, 207, 59, 216, 52, 222, 61 // xor "Sassenrath" #{AA55..}
};
//      kp = seed_str; // Any seed constant.
//      klen = SEED_LEN;
*/

//
//  Cloak: C
//
// Simple data scrambler. Quality depends on the key length.
// Result is made in place (data string).
//
// The key (kp) is passed as a REBVAL or REBYTE (when klen is !0).
//
static REBOOL Cloak(
    REBOOL decode,
    REBYTE *cp,
    REBCNT dlen,
    REBYTE *kp,
    REBCNT klen,
    REBOOL as_is
) {
    REBYTE src[20];
    REBYTE dst[20];

    if (dlen == 0)
        return TRUE;

    REBCNT i;

    // Decode KEY as VALUE field (binary, string, or integer)
    if (klen == 0) {
        REBVAL *val = (REBVAL*)kp;
        REBSER *ser;

        switch (VAL_TYPE(val)) {
        case REB_BINARY:
            kp = VAL_BIN_AT(val);
            klen = VAL_LEN_AT(val);
            break;

        case REB_STRING:
            ser = Temp_Bin_Str_Managed(val, &i, &klen);
            kp = BIN_AT(ser, i);
            break;

        case REB_INTEGER:
            INT_TO_STR(VAL_INT64(val), dst);
            klen = LEN_BYTES(dst);
            as_is = FALSE;
            break;

        default:
            assert(FALSE);
        }

        if (klen == 0)
            return FALSE;
    }

    if (!as_is) {
        for (i = 0; i < 20; i++)
            src[i] = kp[i % klen];

        SHA1(src, 20, dst);
        klen = 20;
        kp = dst;
    }

    if (decode) {
        for (i = dlen - 1; i > 0; i--)
            cp[i] ^= cp[i - 1] ^ kp[i % klen];
    }

    // Change starting byte based all other bytes.

    REBCNT n = 0xa5;
    for (i = 1; i < dlen; i++)
        n += cp[i];

    cp[0] ^= cast(REBYTE, n);

    if (!decode) {
        for (i = 1; i < dlen; i++)
            cp[i] ^= cp[i - 1] ^ kp[i % klen];
    }

    return TRUE;
}


//
//  decloak: native/export [
//
//  {Decodes a binary string scrambled previously by encloak.}
//
//      data [binary!]
//          "Binary series to descramble (modified)"
//      key [string! binary! integer!]
//          "Encryption key or pass phrase"
//      /with
//          "Use a string! key as-is (do not generate hash)"
//  ]
//
static REBNATIVE(decloak)
{
    INCLUDE_PARAMS_OF_DECLOAK;

    if (NOT(Cloak(
        TRUE,
        VAL_BIN_AT(ARG(data)),
        VAL_LEN_AT(ARG(data)),
        cast(REBYTE*, ARG(key)),
        0,
        REF(with)
    ))){
        fail (ARG(key));
    }

    Move_Value(D_OUT, ARG(data));
    return R_OUT;
}


//
//  encloak: native/export [
//
//  "Scrambles a binary string based on a key."
//
//      data [binary!]
//          "Binary series to scramble (modified)"
//      key [string! binary! integer!]
//          "Encryption key or pass phrase"
//      /with
//          "Use a string! key as-is (do not generate hash)"
//  ]
//
static REBNATIVE(encloak)
{
    INCLUDE_PARAMS_OF_ENCLOAK;

    if (NOT(Cloak(
        FALSE,
        VAL_BIN_AT(ARG(data)),
        VAL_LEN_AT(ARG(data)),
        cast(REBYTE*, ARG(key)),
        0,
        REF(with))
    )){
        fail (ARG(key));
    }

    Move_Value(D_OUT, ARG(data));
    return R_OUT;
}

#include "tmp-mod-crypt-last.h"
