REBOL []

name: 'Crypt
loadable: no ;tls depends on this, so it has to be builtin
source: %crypt/mod-crypt.c
includes: reduce [
    (join repo-dir %extensions/crypt/)
    (join repo-dir %extensions/crypt/mbedtls/include/)  ; sub %mbedtls/
    %prep/extensions/crypt/  ; for %tmp-extensions-crypt-init.inc
]
definitions: [
    {MBEDTLS_CONFIG_FILE="mbedtls-rebol-config.h"}
]
depends: [
    [%crypt/mbedtls/library/rsa.c  #no-c++]
    [%crypt/mbedtls/library/rsa_alt_helpers.c  #no-c++]

    ; If you're using a platform that mbedTLS has been designed for,
    ; you can take the standard settings of what "malloc" and "free"
    ; and "printf" are supposed to be.  (Hopefully it won't actually
    ; use printf in release code...)
    ;
    [%crypt/mbedtls/library/platform.c  #no-c++]
    [%crypt/mbedtls/library/platform_util.c  #no-c++]

    ; The current plan is to embed the bignum implementation into Rebol itself
    ; to power its INTEGER! type (when the integers exceed the cell size).
    ; So it should be shareable across the various crypto that uses it.
    ;
    [%crypt/mbedtls/library/bignum.c  #no-c++]

    ; Generic message digest and cipher abstraction layers (write code to one
    ; C interface, get all the digests and ciphers adapted to it for "free",
    ; as well as be able to list and query which ones were built in by name)
    ;
    [%crypt/mbedtls/library/md.c  #no-c++]
    [%crypt/mbedtls/library/cipher.c  #no-c++]
    [%crypt/mbedtls/library/cipher_wrap.c  #no-c++]

    ; MESSAGE DIGESTS
    ;
    ; !!! RC4, MD5, and SHA1 are all considered weak by modern standards.
    ; But they were in R3-Alpha, and outside of bytes taken up in the EXE
    ; don't cost extra to support (the generic MD wrapper handles them)
    ;
    [%crypt/mbedtls/library/sha256.c  #no-c++]
    [%crypt/mbedtls/library/sha512.c  #no-c++]
    [%crypt/mbedtls/library/ripemd160.c  #no-c++]  ; used by BitCoin :-/
    [%crypt/mbedtls/library/md5.c  #no-c++]  ; !!! weak
    [%crypt/mbedtls/library/sha1.c  #no-c++]  ; !!! weak

    ; BLOCK CIPHERS
    ;
    [%crypt/mbedtls/library/aes.c  #no-c++]
    [%crypt/mbedtls/library/arc4.c  #no-c++]  ; !!! weak

    ; !!! Plain Diffie-Hellman(-Merkel) is considered weaker than the
    ; Elliptic Curve Diffie-Hellman (ECDH).  It was an easier first test case
    ; to replace the %dh.h and %dh.c code, however.  Separate extensions for
    ; each crypto again would be preferable.
    ;
    [%crypt/mbedtls/library/dhm.c  #no-c++]

    [
        %crypt/mbedtls/library/ecdh.c
        #no-c++

        <msc:/wd4065>  ; switch contains `default` but no case labels
        ; ^-- (triggered when MBEDTLS_ECDH_LEGACY_CONTEXT is disabled)
    ]
    [%crypt/mbedtls/library/ecp.c  #no-c++]  ; also needed for ECDHE
    [
        %crypt/mbedtls/library/ecp_curves.c
        #no-c++

        <msc:/wd4127>  ; conditional expression is constant
     ]  ; also needed for ECDHE

     ; !!! This is required unless you enable MBEDTLS_ECP_NO_INTERNAL_RNG,
     ; which menacingly refers to opportunities for side channel attacks.
     ;
     [%crypt/mbedtls/library/hmac_drbg.c  #no-c++]
]

libraries: try switch system-config/os-base [
    'Windows [
        ;
        ; Provides crypto functions, e.g. CryptAcquireContext()
        ;
        [%advapi32]
    ]
]
