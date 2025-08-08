(null? delimit #" " [])
("1 2" = delimit #" " [1 2])

(null? delimit "unused" [])
("1" = delimit "unused" [1])
("12" = delimit "" [1 2])

("1^/^/2" = delimit #"^/" ["1^/" "2"])

; Empty text is distinct from SPACE/null
(" A" = delimit ":" [_ "A" opt null])
(":A:" = delimit ":" ["" "A" ""])

; _ is now the literal space character, fetching from variables is valid.
[
    ("a  c" = unspaced ["a" _ comment <b> _ "c"])
    ("a  c" = spaced ["a" space comment <b> space "c"])
    ("a c" = spaced ["a" opt null comment <b> (null else '_) "c"])
    ("a c" = unspaced ["a" opt null comment <b> (null else '_) "c"])
]

; RUNE! does not space
(
    project: 'Ren-C
    bad-thing: "Software Complexity"
    new?: does [project <> 'Rebol]

    str: spaced [#<< project #>> _ "The" (if new? 'NEW) "War On" bad-thing]

    str = "<<Ren-C>> The NEW War On Software Complexity"
)

; Empty groups vaporize and do not add delimiters.  Empty blocks vaporize
; as well.
;
("some**stuff" = delimit "**" [() "some" () "stuff" ()])
("some**stuff" = delimit "**" [[] "some" [] "stuff" []])

; Empty strings do NOT vaporize, because DELIMIT needs to be able to point
; out empty fields.  Use VOID for emptiness.
;
("**some****stuff**" = delimit "**" [-[]- "some" -[]- "stuff" -[]-])
("some**stuff" = delimit "**" [^void "some" ^void "stuff" ^void])

[
    (
        word: "part"
        "Mixing unspacedword with spaced part" = spaced [
            "Mixing" unspaced @["unspaced" word] "with" "spaced" word
        ]
    )
]

[
    ("Hello World" = spaced ["Hello" ^void "World"])
    ("Hello World" = spaced ["Hello" if null ["Cruel"] "World"])
    ("Hello World" = spaced compose ["Hello" (if null ["Cruel"]) "World"])

    ("HelloWorld" = unspaced ["Hello" ^void "World"])
    (
        f: make frame! func [] [return ^ghost]
        "HelloWorld" = unspaced ["Hello" eval f "World"]
    )

    (
        e: sys.util/recover [spaced ["Hello" ~#baddie~ "World"]]
        all [
            e.id = 'bad-antiform
            e.arg1 = '~#baddie~
        ]
    )
]

; DELIMIT:HEAD:TAIL
[
    ("a,b,c," = delimit:tail "," ["a" "b" "c"])
    ("a," = delimit:tail "," "a")

    (",a,b,c" = delimit:head "," ["a" "b" "c"])
    (",a" = delimit:head "," "a")

    (",a,b,c," = delimit:head:tail "," ["a" "b" "c"])
    (",a," = delimit:head:tail "," "a")

    (null = delimit:head:tail "," [^void])
    (null = delimit:head:tail "," ^void)
]

; BLOCK!s to subvert delimiting (works one level deep)
[
    ("a bc d" = spaced ["a" ["b" "c"] "d"])

    ~???~ !! (
        block: ["b" "c"]
        spaced ["a" block "d"]
    )
]

; @ Now Means Mold
[
    (
        block: [a b c]
        "The block is: [a b c]" = spaced ["The block is:" @block]
    )
    (
        block: [a b c]
        "The block is: [c b a]" = spaced ["The block is:" @(reverse block)]
    )
    (
        obj: make object! [block: [a b c]]
        "The block is: [a b c]" = spaced ["The block is:" @obj.block]
    )
]

; @(spread []) should vanish
;
("ab" = unspaced ["a" @(spread []) "b"])

(null? spaced [])
(null? spaced [comment "hi"])
(null? spaced [()])
