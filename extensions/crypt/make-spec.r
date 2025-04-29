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
    --{MBEDTLS_CONFIG_FILE="mbedtls-rebol-config.h"}--
]

includes: [
    ;
    ; Actual includes are in a subdirectory, e.g.
    ;
    ;     #include "mbedtls/xxx.h" => %mbedtls/include/mbedtls/xxx.h
    ;
    %mbedtls/include/
]

sources: %mod-crypt.c

depends: [
    ;
    ; The oid.c dependency in RSA is contingent on #define MBEDTLS_PKCS1_V15
    ; padding implementation.
    ;
    [%mbedtls/library/rsa.c  #no-c++]
    [%mbedtls/library/rsa_alt_helpers.c  #no-c++]
    [%mbedtls/library/oid.c  #no-c++]
    [%tf_snprintf.c  #no-c++]

    ; If you're using a platform that mbedTLS has been designed for,
    ; you can take the standard settings of what "malloc" and "free"
    ; and "printf" are supposed to be.  (Hopefully it won't actually
    ; use printf in release code...)
    ;
    [%mbedtls/library/platform.c  #no-c++]
    [%mbedtls/library/platform_util.c  #no-c++]

    ; The current plan is to embed the bignum implementation into Rebol itself
    ; to power its INTEGER! type (when the integers exceed the cell size).
    ; So it should be shareable across the various crypto that uses it.
    ;
    [%mbedtls/library/bignum.c  #no-c++]
    [%mbedtls/library/constant_time.c  #no-c++]

    ; Generic message digest and cipher abstraction layers (write code to one
    ; C interface, get all the digests and ciphers adapted to it for "free",
    ; as well as be able to list and query which ones were built in by name)
    ;
    [%mbedtls/library/md.c  #no-c++]
    [%mbedtls/library/cipher.c  #no-c++]
    [%mbedtls/library/cipher_wrap.c  #no-c++]

    ; MESSAGE DIGESTS
    ;
    ; !!! MD5 and SHA1 are considered weak by modern standards.  But they were
    ; in R3-Alpha, and outside of bytes taken up in the EXE don't cost extra to
    ; support (the generic MD wrapper handles them).  RC4 was once also
    ; included "just because it was there", but mbedTLS 3 dropped it.
    ;
    [%mbedtls/library/sha256.c  #no-c++]
    [%mbedtls/library/sha512.c  #no-c++]
    [%mbedtls/library/ripemd160.c  #no-c++]  ; used by BitCoin :-/
    [%mbedtls/library/md5.c  #no-c++]  ; !!! weak
    [%mbedtls/library/sha1.c  #no-c++]  ; !!! weak

    ; BLOCK CIPHERS
    [
        %mbedtls/library/aes.c
        #no-c++

        <msc:/analyze->  ; trips up static analyzer
    ]

    ; !!! Plain Diffie-Hellman(-Merkel) is considered weaker than the
    ; Elliptic Curve Diffie-Hellman (ECDH).  It was an easier first test case
    ; to replace the %dh.h and %dh.c code, however.  Separate extensions for
    ; each crypto again would be preferable.
    ;
    [%mbedtls/library/dhm.c  #no-c++]

    [
        %mbedtls/library/ecdh.c
        #no-c++

        <msc:/wd4065>  ; switch contains `default` but no case labels
        ; ^-- (triggered when MBEDTLS_ECDH_LEGACY_CONTEXT is disabled)
    ]
    [%mbedtls/library/ecp.c  #no-c++]  ; also needed for ECDHE
    [
        %mbedtls/library/ecp_curves.c
        #no-c++

        <msc:/wd4127>  ; conditional expression is constant
        <msc:/analyze->  ; trips up static analyzer
     ]  ; also needed for ECDHE

     ; !!! This is required unless you enable MBEDTLS_ECP_NO_INTERNAL_RNG,
     ; which menacingly refers to opportunities for side channel attacks.
     ;
     [%mbedtls/library/hmac_drbg.c  #no-c++]
]

libraries: switch platform-config.os-base [
    'Windows [
        ;
        ; Provides crypto functions, e.g. CryptAcquireContext()
        ;
        [%advapi32]
    ]
]
