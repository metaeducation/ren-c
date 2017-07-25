REBOL [
    Title: "Crypt Extension"
    name: 'Crypt
    type: 'Extension
    version: 1.0.0
    license: {Apache 2.0}
]

hmac-sha256: function [
    {computes the hmac-sha256 for message m using key k}

    k [binary!] 
    m [binary!]
][
    key: copy k
    message: copy m
    blocksize: 64
    if (length key) > blocksize [
        key: sha256 key
    ]
    if (length key) < blocksize [
        insert/dup tail key #{00} (blocksize - length key)
    ]
    insert/dup opad: copy #{} #{5C} blocksize
    insert/dup ipad: copy #{} #{36} blocksize
    o_key_pad: XOR~ opad key
    i_key_pad: XOR~ ipad key
    sha256 join-of o_key_pad sha256 join-of i_key_pad message
]

append lib compose [rsa-make-key: (func [
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
])]

append lib compose [dh-make-key: (func [
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
])]
