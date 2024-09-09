(null? delimit #" " [])
("1 2" = delimit #" " [1 2])

(null? delimit "unused" [])
("1" = delimit "unused" [1])
("12" = delimit "" [1 2])

("1^/^/2" = delimit #"^/" ["1^/" "2"])

; Empty text is distinct from BLANK/null
(" A" = delimit ":" [_ "A" maybe null])
(":A:" = delimit ":" ["" "A" ""])

; now all blanks act as spaces in string contexts
[
    ("a  c" = spaced ["a" _ comment <b> _ "c"])
    ("a  c" = spaced ["a" blank comment <b> blank "c"])
    ("a c" = spaced ["a" maybe null comment <b> (null else '_) "c"])
]

; ISSUE! is to be merged with CHAR! and does not space
(
    project: 'Ren-C
    bad-thing: "Software Complexity"
    new?: does [project <> 'Rebol]

    str: spaced [#<< project #>> _ {The} (if new? 'NEW) {War On} bad-thing]

    str = "<<Ren-C>> The NEW War On Software Complexity"
)

; Empty groups vaporize and do not add delimiters.  There is no assumption
; made on what blocks do--user must convert.
;
("some**stuff" = delimit "**" [() "some" () "stuff" ()])
~???~ !! ("some**stuff" = delimit "**" [[] "some" [] "stuff" []])

; Empty strings do NOT vaporize, because DELIMIT needs to be able to point
; out empty fields.  Use VOID for emptiness.
;
("**some****stuff**" = delimit "**" [{} "some" {} "stuff" {}])
("some**stuff" = delimit "**" [void "some" void "stuff" void])

[
    (
        word: "part"
        "Mixing unspacedword with spaced part" = spaced [
            "Mixing" unspaced @["unspaced" word] "with" "spaced" word
        ]
    )
]

[
    ("Hello World" = spaced ["Hello" void "World"])
    ("Hello World" = spaced ["Hello" if false ["Cruel"] "World"])
    ("Hello World" = spaced compose ["Hello" (if false ["Cruel"]) "World"])

    ("HelloWorld" = unspaced ["Hello" void "World"])
    (
        f: make frame! :nihil
        "HelloWorld" = unspaced ["Hello" eval f "World"]
    )

    (
        e: sys.util/rescue [spaced ["Hello" ~baddie~ "World"]]
        all [
            e.id = 'bad-antiform
            e.arg1 = '~baddie~
        ]
    )
]

; DELIMIT/HEAD/TAIL
[
    ("a,b,c," = delimit/tail "," ["a" "b" "c"])
    ("a," = delimit/tail "," "a")

    (",a,b,c" = delimit/head "," ["a" "b" "c"])
    (",a" = delimit/head "," "a")

    (",a,b,c," = delimit/head/tail "," ["a" "b" "c"])
    (",a," = delimit/head/tail "," "a")

    (null = delimit/head/tail "," [void])
    (null = delimit/head/tail "," void)
]
