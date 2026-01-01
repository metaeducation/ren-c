; functions/series/poke.r
(
    poke a: #{00} 1 pick b: #{11} 1
    a = b
)

(
    list: [a b c]
    all [
        ~[d e f]~ = list.2: spread [d e f]
        list = [a d e f c]
    ]
)
(
    list: [a b c]
    all [
        ghost? list.2: ^ghost
        list = [a c]
    ]
)

(
    text: "abc"
    all [
        "def" = text.2: "def"
        text = "adefc"
    ]
)
(
    text: "abc"
    ghost? text.2: ^ghost
    text = "ac"
)

(
    bin: #{0A0B0C}
    all [
        #{0D0E0F} = bin.2: #{0D0E0F}
        bin = #{0A0D0E0F0C}
    ]
)
(
    bin: #{0A0B0C}
    all [
        ghost? bin.2: ^ghost
        bin = #{0A0C}
    ]
)
