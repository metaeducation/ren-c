REBOL [
    Title: "Crypt Extension"
    Name: Crypt
    Type: Module
    Version: 1.0.0
    License: {Apache 2.0}
]

; !!! This should also call SHUTDOWN-CRYPTO at some point (module finalizer?)
;
init-crypto


rsa-make-key: func [
    {Creates a key object for RSA algorithm.}
][
    make object! [
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


; !!! Kludgey export mechanism; review correct approach for modules
;
export [rsa-make-key]
