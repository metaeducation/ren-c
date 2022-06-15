; AES streaming cipher tests

[
    (test: func [data check] [
        bin: as binary! data
        bin-len: length of bin
        key-128: #{01020304050607080910111213141516}
        ctx: aes-key key-128 _
        encrypted: aes-stream ctx as binary! data
        ctx: aes-key/decrypt key-128 _
        decrypted: aes-stream ctx encrypted
        return did all [
            bin = copy/part decrypted bin-len
            check = encrypted
        ]
    ] true)

    ; exactly one block
    (test "1234567890123456" #{4538B1F7577E37CB4404D266384524BB})

    ; one byte less than a block
    (test "123456789012345" #{1E6EC2BAC1019FA692B8DAC5A5E505E8})

    ; one byte more than a block
    (test "12345678901234567"
        #{4538B1F7577E37CB4404D266384524BB7409AEFAE8995925B03F8216E7B92F67})
]
