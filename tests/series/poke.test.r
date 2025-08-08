; functions/series/poke.r
(
    poke a: #{00} 1 pick b: #{11} 1
    a = b
)

(
    list: [a b c]
    all [
        ~(d e f)~ = list.2: spread [d e f]
        list = [a d e f c]
    ]
)
(
    list: [a b c]
    all [
        void? list.2: ^void
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
    void? text.2: ^void
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
        void? bin.2: ^void
        bin = #{0A0C}
    ]
)
