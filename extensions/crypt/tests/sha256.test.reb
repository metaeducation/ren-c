; SHA256 small smoke test
; (Implementation comes from mbedTLS which tests the algorithm itself)

[
    (pairs: [
        ; Edge case: empty string or binary
        ;
        {} #{e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855}
        #{} #{e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855}

        ; Simple non-empty cases
        ;
        "Rebol" #{C8537DEDCA2810F48C80008DBCBDA9AC2FA60382C7F073118DDDEDEEEE65FF47}
        #{1020BFDBFD0304} #{165825199DB849EAFE254E3339FD651748EBF845CAD94C238424EAF344647F98}
    ] true)

    ; plain hash test
    (
        for-each [data hash] pairs [
            if hash != sha256 data [
                fail ["bad sha256 for" mold data]
            ]
        ]
        true
    )

    ; non-series head test
    (
        for-each [data hash] pairs [
            longer: join either binary? data [#{57911240}] ["MetÆ"] data
            if hash != sha256 skip longer 4 [
                fail ["bad sha256 for skip 4 of" mold longer]
            ]
        ]
        true
    )
]


(
    ; This is the usermode implementation of SHA256 with a HMAC (e.g.
    ; integrating a password into the hashing process).  That is now supported
    ; by the mbedTLS generalized %md.h interface for all hashes.  But rather
    ; than get rid of the usermode sha256 code, this just tests it against
    ; the native version (which is much more likely to be testing if this
    ; code breaks for some reason than anything wrong with mbedTLS).
    ;
    hmac-sha256: function [
        {computes the hmac-sha256 for message m using key k}

        m [binary! text!]
        k [binary! text!]
    ][
        key: as binary! copy k
        message: as binary! copy m
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
        return sha256 join o_key_pad (sha256 join i_key_pad message)
    ]

    random/seed "Deterministic Behavior Desired"
    repeat 100 [
        data-len: random 1024
        data: make binary! data-len
        repeat data-len [append data (random 256) - 1]

        key-len: random 512
        key: make binary! key-len
        repeat key-len [append data (random 256) - 1]

        a: hmac-sha256 data key
        b: checksum/key 'sha256 data key
        if a != b [
            fail ["Mismatched HMAC-SHA256 for" mold data "with" mold key]
        ]
    ]
    true
)
