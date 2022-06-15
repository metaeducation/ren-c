REBOL [
    Title: "Crypt Extension"
    Name: Crypt
    Type: Module
    Version: 1.0.0
    License: {Apache 2.0}
]

export rsa-make-key: func [
    {Creates a key object for RSA algorithm.}
][
    return make object! [
        padding:    ;spec block for pad, e.g. [raw] or [pcks1-v15 #md5]
        n:          ;modulus
        e:          ;public exponent

        d:          ;private exponent
        p:          ;prime num 1
        q:          ;prime num 2

        ; The parameters for the "Chinese Remainder Theorem" are not strictly
        ; necessary for decoding RSA.  But if they are captured during the
        ; generation process and passed to the decoder, they will accelerate
        ; the decoding.  This was a suggested extension that makes it practical
        ; to choose larger keys for security against discovered attacks.
        ;
        dp:         ;CRT exponent 1
        dq:         ;CRT exponent 2
        qinv:       ;CRT coefficient
    ]
]
