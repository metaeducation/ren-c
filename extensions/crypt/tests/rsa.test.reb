; %rsa.test.reb
;
; https://forum.rebol.info/t/1812

; Test the raw mode RSA with a small example.
[
    ; use /INSECURE to override errors that tell you the key is too small
    ; (makes examples more readable)
    (
        [public private]: rsa-generate-keypair/padding/insecure 128 [raw]
        16 = length of public.n
    )

    ; When you use raw mode encryption, the data you encrypt must be
    ; *exactly the same size* as the key you made.
    (
        plaintext: #{0123456789ABCDEFFEDCBA9876543210}
        16 = length of plaintext
    )

    ; Encrypted output will be the same size as the key (whether raw or not)
    (
        encrypted: rsa-encrypt plaintext public
        16 = length of encrypted
    )

    ; Should return same result every time...
    ;
    (repeat 10 [
        cryptcheck: rsa-encrypt plaintext private
        if cryptcheck <> encrypted [
            fail [
                "Different encrypt:" mold cryptcheck "vs" mold encrypted
            ]
        ]
    ], true)

    (plaintext = rsa-decrypt encrypted private)
]

; Check the defaulting for PKCS1-V15
(
    [public private]: rsa-generate-keypair 1028
    did all [
        public.padding = [pkcs1-v15]
        private.padding = [pkcs1-v15]
    ]
)

; Now try with key, padding, and hash combinations
(
    hash-sizes: [#md5 16 #sha512 64]

    rsa-capacity: func [num-key-bits padding] [
        hash-size: if not second padding [
            0
        ] else [
            select hash-sizes second padding
        ]

        let num-key-bytes: num-key-bits / 8

        let capacity: switch first padding [
            'pkcs1-v15 [
                num-key-bytes - 11  ; defined by the spec
            ]
            'pkcs1-v21 [
                ; https://crypto.stackexchange.com/a/42100
                ; You lose a lot of space with big hashes! :-/
                num-key-bytes - (2 + (hash-size * 2))
            ]
        ] else [
            num-key-bytes
        ]
        if capacity < 0 [
            return 0
        ]
        return capacity
    ]

    for-each num-key-bits [1024 2048 4096] [
        for-each padding [  ; not just a "padding" scheme, but they call it that
            [pkcs1-v15]
            [pkcs1-v15 #md5]
            [pkcs1-v15 #sha512]

            ; Can't use pkcs1-v21 without hashing as just `[pkcs1-v21]`
            [pkcs1-v21 #md5]
            [pkcs1-v21 #sha512]
        ][
            ; Skip impossible combinations (512 bit hash needs two hashes
            ; in pkcs1-v21, plus two bytes, and 1024 isn't enough for that)
            ;
            capacity: rsa-capacity num-key-bits padding
            if capacity = 0 [
                comment [print ["Impossible, skip:" num-key-bits mold padding]]
                continue
            ]

            [public private]: rsa-generate-keypair/padding num-key-bits padding
            assert [num-key-bits = length of public.n * 8]

            plaintext: copy #{}
            repeat (random capacity) - 1 [
                append plaintext (random 256) - 1
            ]

            ; Should return a different result (most) every time...if it ever
            ; happens incidentally that's an incredibly rare fluke.
            ;
            encrypted-list: copy []
            repeat 10 [
                encrypted: rsa-encrypt plaintext public
                if find encrypted-list encrypted [
                    fail [
                        "Duplicate encrypt:" mold encrypted
                    ]
                ]
                decrypted: rsa-decrypt encrypted private
                if decrypted <> plaintext [
                    fail [
                        "Decrypt expected:" mold plaintext "got" mold decrypted
                    ]
                ]
            ]
        ]
    ]

    true
)
