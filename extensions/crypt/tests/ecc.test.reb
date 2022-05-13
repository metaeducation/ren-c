; Elliptic curve cryptography

(
    repeat 32 [
        for-each group [secp256r1 curve25519] [
            a: ecc-generate-keypair group
            b: ecc-generate-keypair group
            a-secret: ecdh-shared-secret group a.private-key b.public-key
            b-secret: ecdh-shared-secret group b.private-key a.public-key
            if a-secret <> b-secret [
                fail ["secrets did not match for" group]
            ]
        ]
    ]
)
