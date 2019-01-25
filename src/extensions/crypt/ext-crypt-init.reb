REBOL [
    Title: "Crypt Extension"
    Name: Crypt
    Type: Module
    Options: [isolate]
    Version: 1.0.0
    License: {Apache 2.0}
]

; !!! This should also call SHUTDOWN-CRYPTO at some point (module finalizer?)
;
init-crypto

hmac-sha256: function [
    {computes the hmac-sha256 for message m using key k}

    k [binary!]
    m [binary!]
][
    key: copy k
    message: copy m
    blocksize: 64
    if blocksize < length of key [
        key: sha256 key
    ]
    if blocksize > length of key [
        insert/dup tail key #{00} (blocksize - length of key)
    ]
    insert/dup opad: copy #{} #{5C} blocksize
    insert/dup ipad: copy #{} #{36} blocksize
    o_key_pad: opad xor+ key
    i_key_pad: ipad xor+ key
    sha256 join o_key_pad (sha256 join i_key_pad message)
]


rsa-make-key: func [
    {Creates a key object for RSA algorithm.}
][
    has [
        n:          ;modulus
        e:          ;public exponent
        d:          ;private exponent
        p:          ;prime num 1
        q:          ;prime num 2
        dp:         ;CRT exponent 1
        dq:         ;CRT exponent 2
        qinv:       ;CRT coefficient
        _
    ]
]


dh-make-key: func [
    {Creates a key object for Diffie-Hellman algorithm.}
;NOT YET IMPLEMENTED
;   /generate
;       size [integer!] \"Key length\"
;       generator [integer!] \"Generator number\"
][
    has [
        priv-key:   ;private key
        pub-key:    ;public key
        g:          ;generator
        p:          ;prime modulus
        _
    ]
]


; !!! Kludgey export mechanism; review correct approach for modules
;
sys/export [hmac-sha256 rsa-make-key dh-make-key]
