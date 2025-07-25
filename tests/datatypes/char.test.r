; datatypes/char.r
(rune! = type of #"a")

(char? #"a")
(char? #a)
(not char? 1)
(not char? #aa)

; Only length 1 runes should register as CHAR?
(
    for-each [rune length size] [
        #b 1 1
        #à 1 2
        #漢 1 3
        #😺 1 4

        #bà 2 3
        #😺😺 2 8
        #漢à😺 3 9

        #12345678901234567890 20 20  ; longer than fits in cell
    ][
        assert [length = length of rune]
        assert [size = size of rune]
        assert [(char? rune) = (1 = length of rune)]
        assert [rune = copy rune]
    ]
    ok
)

; Math operations should only work on single characters
[
    (#a + 1 = #b)

    ~expect-arg~ !! (  ; was ~cannot-use~ before
        #aa + 1
    )
]

~illegal-zero-byte~ !! (
    eval load #{23225E4022203D2023225E2830302922}  ; ^ @ = ^ (00)
)
~illegal-zero-byte~ !! (
    eval load #{23225E286E756C6C2922203D2023225E2830302922}  ; ^ (null) = ^ (00)
)
~illegal-zero-byte~ !! (
    transcode:one #{23225E2830302922}  ; ^ (00)
)

(#"^A" = #"^(01)")
(#"^B" = #"^(02)")
(#"^C" = #"^(03)")
(#"^D" = #"^(04)")
(#"^E" = #"^(05)")
(#"^F" = #"^(06)")
(#"^G" = #"^(07)")
(#"^H" = #"^(08)")
(#"^I" = #"^(09)")
(#"^J" = #"^(0A)")
(#"^K" = #"^(0B)")
(#"^L" = #"^(0C)")
(#"^M" = #"^(0D)")
(#"^N" = #"^(0E)")
(#"^O" = #"^(0F)")
(#"^P" = #"^(10)")
(#"^Q" = #"^(11)")
(#"^R" = #"^(12)")
(#"^S" = #"^(13)")
(#"^T" = #"^(14)")
(#"^U" = #"^(15)")
(#"^V" = #"^(16)")
(#"^W" = #"^(17)")
(#"^X" = #"^(18)")
(#"^Y" = #"^(19)")
(#"^Z" = #"^(1A)")
(#"^[" = #"^(1B)")
(#"^\" = #"^(1C)")
(#"^]" = #"^(1D)")
(#"^!" = #"^(1E)")
(#"^_" = #"^(1F)")
(#" " = #"^(20)")
(#"!" = #"^(21)")
(#"^"" = #"^(22)")
(#"#" = #"^(23)")
(#"$" = #"^(24)")
(#"%" = #"^(25)")
(#"&" = #"^(26)")
(#"'" = #"^(27)")
(#"(" = #"^(28)")
(#")" = #"^(29)")
(#"*" = #"^(2A)")
(#"+" = #"^(2B)")
(#"," = #"^(2C)")
(#"-" = #"^(2D)")
(#"." = #"^(2E)")
(#"/" = #"^(2F)")
(#"0" = #"^(30)")
(#"1" = #"^(31)")
(#"2" = #"^(32)")
(#"3" = #"^(33)")
(#"4" = #"^(34)")
(#"5" = #"^(35)")
(#"6" = #"^(36)")
(#"7" = #"^(37)")
(#"8" = #"^(38)")
(#"9" = #"^(39)")
(#":" = #"^(3A)")
(#";" = #"^(3B)")
(#"<" = #"^(3C)")
(#"=" = #"^(3D)")
(#">" = #"^(3E)")
(#"?" = #"^(3F)")
(#"@" = #"^(40)")
(#"A" = #"^(41)")
(#"B" = #"^(42)")
(#"C" = #"^(43)")
(#"D" = #"^(44)")
(#"E" = #"^(45)")
(#"F" = #"^(46)")
(#"G" = #"^(47)")
(#"H" = #"^(48)")
(#"I" = #"^(49)")
(#"J" = #"^(4A)")
(#"K" = #"^(4B)")
(#"L" = #"^(4C)")
(#"M" = #"^(4D)")
(#"N" = #"^(4E)")
(#"O" = #"^(4F)")
(#"P" = #"^(50)")
(#"Q" = #"^(51)")
(#"R" = #"^(52)")
(#"S" = #"^(53)")
(#"T" = #"^(54)")
(#"U" = #"^(55)")
(#"V" = #"^(56)")
(#"W" = #"^(57)")
(#"X" = #"^(58)")
(#"Y" = #"^(59)")
(#"Z" = #"^(5A)")
(#"[" = #"^(5B)")
(#"\" = #"^(5C)")
(#"]" = #"^(5D)")
(#"^^" = #"^(5E)")
(#"_" = #"^(5F)")
(#"`" = #"^(60)")
(#"a" = #"^(61)")
(#"b" = #"^(62)")
(#"c" = #"^(63)")
(#"d" = #"^(64)")
(#"e" = #"^(65)")
(#"f" = #"^(66)")
(#"g" = #"^(67)")
(#"h" = #"^(68)")
(#"i" = #"^(69)")
(#"j" = #"^(6A)")
(#"k" = #"^(6B)")
(#"l" = #"^(6C)")
(#"m" = #"^(6D)")
(#"n" = #"^(6E)")
(#"o" = #"^(6F)")
(#"p" = #"^(70)")
(#"q" = #"^(71)")
(#"r" = #"^(72)")
(#"s" = #"^(73)")
(#"t" = #"^(74)")
(#"u" = #"^(75)")
(#"v" = #"^(76)")
(#"w" = #"^(77)")
(#"x" = #"^(78)")
(#"y" = #"^(79)")
(#"z" = #"^(7A)")
(#"{" = #"^(7B)")
(#"|" = #"^(7C)")
(#"}" = #"^(7D)")
(#"~" = #"^(7E)")
(#"^~" = #"^(7F)")
; alternatives


(#"^(line)" = #"^(0A)")
(#"^/" = #"^(0A)")
(#"^(tab)" = #"^(09)")
(#"^-" = #"^(09)")
(#"^(page)" = #"^(0C)")
(#"^(esc)" = #"^(1B)")
(#"^(back)" = #"^(08)")
(#"^(del)" = #"^(7f)")

; Quotes are removed if not necessary in molding
(-[#a]- = mold #"a")
(-[#a]- = mold #a)

(
    c: make-char 0
    all [
        char? c
        0 = codepoint of c
    ]
)

(char? #"^(ff)")  ; no longer the maximum

(0 = subtract NUL NUL)
(-1 = subtract NUL #"^(01)")
(-255 = subtract NUL #"^(ff)")
(1 = subtract #"^(01)" NUL)
(0 = subtract #"^(01)" #"^(01)")
(-254 = subtract #"^(01)" #"^(ff)")
(255 = subtract #"^(ff)" NUL)
(254 = subtract #"^(ff)" #"^(01)")
(0 = subtract #"^(ff)" #"^(ff)")

~codepoint-negative~ !! (
    NUL - 1
)
~codepoint-negative~ !! (
    NUL + -1
)

(#"Ā" = add #"^(01)" #"^(ff)")
(#"Ā" = add #"^(ff)" #"^(01)")
(#"Ǿ" = add #"^(ff)" #"^(ff)")

(
    randomize "let's be deterministic"
    codepoints: [
        #"b"  ; 1 utf-8 byte
        #"à"  ; 2 utf-8 bytes encoded
        #"漢"  ; 3 utf-8 bytes encoded
        #"😺"  ; 4 utf-8 bytes encoded
    ]
    count-up 'size 4 wrap [
        c: codepoints.(size)
        if size != length of as blob! c [
            panic "test character doesn't match expected size"
        ]
        count-up 'len 64 wrap [
            s: copy ""
            e: copy ""
            picks: copy []
            count-up 'i len [
                append s random-pick codepoints
                append e c
                append picks i
            ]
            shuffle picks  ; randomize positions so not always in order
            for-each 'i picks [
                comment [
                    print ["Trying" i "/" len "in" mold s]
                ]
                s.(i): c
                if len != length of s [
                    panic ["Length not" len "for" mold s]
                ]
            ]
            if not s = e [panic ["Mismatch:" mold s "=>" mold e]]
        ]
        ok
    ]
)


[#1031
    ~not-one-codepoint~ !! (to-char #{})

    ; 1 UTF-8 byte
    (#"b" = to-char #{62})
    (#{62} = as blob! #"b")

    ; 2 UTF-8 bytes
    (#"à" = to-char #{C3A0})
    (#{C3A0} = as blob! #"à")

    ; 3 UTF-8 bytes
    (#"漢" = to-char #{E6BCA2})
    (#{E6BCA2} = as blob! #"漢")

    ; 4 UTF-8 bytes
    (#"😺" = to-char #{F09F98BA})
    (#{F09F98BA} = as blob! #"😺")
]
