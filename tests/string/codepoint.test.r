; %codepoint.test.r


(
    for-each [c cp bin] [
        #b   98      #{62}
        #à   224     #{C3A0}
        #漢  28450   #{E6BCA2}
        #😺  128570  #{F09F98BA}
    ] wrap [
        assert [cp = codepoint of c]
        assert [bin = as blob! c]
        assert [cp = codepoint of bin]

        bincopy: encode 'UTF-8 c
        assert [bincopy = bin]
        assert [cp = codepoint of bincopy]
        bincopy: insert bincopy #{AA}
        assert [cp = codepoint of bincopy]  ; non-head ok
        append bincopy #{BB}
        assert [error? codepoint of bincopy]  ; tail data bad

        txtcopy: to text! c
        assert [cp = codepoint of txtcopy]
        txtcopy: insert txtcopy "A"
        assert [cp = codepoint of txtcopy]  ; non-head ok
        append txtcopy "B"
        assert [error? codepoint of txtcopy]  ; tail data bad
    ]
    ok
)

(0 = codepoint of #{00})

~illegal-zero-byte~ !! (as text! #{00})
