; REWORD
; See https://stackoverflow.com/q/14924801/

("This is that." = reword "$1 is $2." [1 "This" 2 "that"])

("A fox is brown." = reword/escape "A %%a is %%b." [a "fox" b "brown"] "%%")

(
    "BrianH is answering Adrian." = reword/escape "I am answering you." [
        "I am" "BrianH is"
        you "Adrian"
    ] ""
)(
    "Hello is Goodbye" = reword/escape "$$$a$$$ is $$$b$$$" [
       a Hello
       b Goodbye
    ] ["$$$" "$$$"]
)

; Functions can optionally take the keyword being replaced
(
    "zero is one-B" = reword "$A is $B" reduce [
        "A" lambda [] ["zero"]
        "B" lambda [w] [join "one-" w]
    ]
)

[
    https://github.com/metaeducation/ren-c/issues/1005
    ("ò" = reword "ò$a" reduce ['a ""])
]

[#2333 (
    subs: ["1" "foo" "10" "bar"]
    text: "$<10>"
    "bar" = reword/escape text subs ["$<" ">"]
)]
