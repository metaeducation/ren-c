Rebol [
    name: Crypt
    notes: "See %extensions/README.md for the format and fields of this file"
]

loadable: 'no  ; TLS depends on this, so it has to be builtin

use-librebol: 'yes

; There are *many* mbedtls configuration #defines.  They provide a monolithic
; config file as an example which has every option, and comments about them.
; We use an adjusted version of the config file which keeps all the options
; and all the comments, with changes annotated.
;
definitions: [
    --[MBEDTLS_CONFIG_FILE="mbedtls-rebol-config.h"]--
]

includes: [
    ;
    ; Actual includes are in a subdirectory, e.g.
    ;
    ;     #include "mbedtls/xxx.h" => %mbedtls/include/mbedtls/xxx.h
    ;
    %mbedtls/include/
]

sources: [mod-crypt.c]

depends: collect [  ; add common options and path prefix to files in list
    let common: [
        #no-c++

        ; mbedTLS claims that there's some reason why they can't avoid making
        ; this warning in GCC:
        ;
        ; https://github.com/Mbed-TLS/mbedtls/issues/6910
        ;
        <gcc:-Wno-redundant-decls>
    ]

    keep spread compose [tf_snprintf.c (common)]  ; not in mbedtls/library dir

    let file  ; no LET in PARSE3, have to put LETs outside
    let block

  parse3 [  ; files in %mbedtls/library using `common` switches
    ;
    ; Modern trends move away from RSA for key exchange (ECDHE is faster, and
    ; has smaller keys).  Also not used for bulk encryption (AES is standard).
    ;
    ; However, RSA is still used for certificate signing (PKI infrastructure
    ; is heavily RSA-based) and digital signatures (widespread tooling).
    ;
    rsa.c
    rsa_alt_helpers.c
    oid.c  ; dependency contingent on `#define MBEDTLS_PKCS1_V15` padding
    asn1parse.c  ; !!! ASN1 PARSING SHOULD NOT BE REQUIRED TO USE RSA!
    asn1write.c  ; !!! should not be needed

    ; If you're using a platform that mbedTLS has been designed for,
    ; you can take the standard settings of what "malloc" and "free"
    ; and "printf" are supposed to be.  (Hopefully it won't actually
    ; use printf in release code...)
    ;
    platform.c
    platform_util.c

    ; Bignums are required for modern cryptography, and mbedTLS has its own
    ; relatively light implementation.  (Once it was planned to share this
    ; code with INTEGER! to implement BigNums, but use of mbedTLS in the
    ; future given their 4.0 divergence is in question.)
    ;
    bignum.c
    bignum_core.c
    constant_time.c

    ; Generic message digest and cipher abstraction layers (write code to one
    ; C interface, get all the digests and ciphers adapted to it for "free",
    ; as well as be able to list and query which ones were built in by name)
    ;
    md.c
    cipher.c [<msc:/wd4389>]  ; signed/unsigned equality testing
    cipher_wrap.c

    ; MESSAGE DIGESTS
    ;
    ; !!! MD5 and SHA1 are considered weak by modern standards.  But they were
    ; in R3-Alpha, and outside of bytes taken up in the EXE don't cost extra to
    ; support (the generic MD wrapper handles them).  RC4 was once also
    ; included "just because it was there", but mbedTLS 3 dropped it.
    ;
    sha256.c
    sha512.c
    ripemd160.c  ; used by BitCoin :-/
    md5.c  ; !!! weak
    sha1.c  ; !!! weak

    ; BLOCK CIPHERS
    ;
    aes.c [
        <msc:/analyze->  ; trips up static analyzer
    ]

    ; !!! Plain Diffie-Hellman(-Merkel) is considered weaker than the
    ; Elliptic Curve Diffie-Hellman (ECDH).  It was an easier first test case
    ; to replace the %dh.h and %dh.c code, however.  Separate extensions for
    ; each crypto again would be preferable.
    ;
    dhm.c [#no-c++]

    ecdh.c [
        <msc:/wd4065>  ; switch contains `default` but no case labels
        ; ^-- (triggered when MBEDTLS_ECDH_LEGACY_CONTEXT is disabled)
    ]
    ecp.c  ; also needed for ECDHE
    ecp_curves.c [
        <msc:/wd4127>  ; conditional expression is constant
        <msc:/wd4388>  ; signed/unsigned mismatch
        <msc:/analyze->  ; trips up static analyzer
     ]  ; also needed for ECDHE

    ; Galois Counter Mode frequently has hardware acceleration, making it
    ; a common choice in TLS 1.2 implementations, supplanting legacy CBC.
    ;
    gcm.c [
        <msc:/wd4065>  ; switch contains 'default' but no 'case' labels
    ]

    ; ChaCha20-Poly1305 offers excellent performance in software-only
    ; implementations, particularly advantageous on platforms lacking AES
    ; hardware acceleration or when facing potential cache-timing attacks on
    ; AES implementations.  It has gained significant traction and is becoming
    ; increasingly prevalent in protocols like TLS 1.3 as a strong alternative
    ; to AES-GCM.
    ;
    chacha20.c
    poly1305.c
    chachapoly.c

    ; !!! This is required unless you enable MBEDTLS_ECP_NO_INTERNAL_RNG,
    ; which menacingly refers to opportunities for side channel attacks.
    ;
    hmac_drbg.c
] [
  some [
    file: [tuple! | path!], block: try block! (
        keep join %mbedtls/library/ to file! file
        keep append (copy common) spread any [block []]
    )
  ]
] ]  ; end COLLECT

libraries: switch platform-config.os-base [
    'Windows [
        ;
        ; Provides crypto functions, e.g. CryptAcquireContext()
        ;
        [%advapi32]
    ]
]
