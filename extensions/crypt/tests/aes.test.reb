; AES streaming cipher tests

[
    (test: func [data check] [
        let bin: as blob! data
        let bin-len: length of bin
        let key-128: #{01020304050607080910111213141516}
        let e: aes-key key-128 _
        let encrypted: aes-stream e as blob! data
        let d: aes-key:decrypt key-128 _
        let decrypted: aes-stream d encrypted
        return all [
            bin = copy:part decrypted bin-len
            check = encrypted
        ]
    ] ok)

    ; exactly one block
    (test "1234567890123456" #{4538B1F7577E37CB4404D266384524BB})

    ; one byte less than a block
    (test "123456789012345" #{1E6EC2BAC1019FA692B8DAC5A5E505E8})

    ; one byte more than a block
    (test "12345678901234567"
        #{4538B1F7577E37CB4404D266384524BB7409AEFAE8995925B03F8216E7B92F67})
]
