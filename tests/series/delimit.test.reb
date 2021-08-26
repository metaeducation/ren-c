(null? delimit #" " [])
("1 2" = delimit #" " [1 2])

(null? delimit "unused" [])
("1" = delimit "unused" [1])
("12" = delimit "" [1 2])

("1^/^/2" = delimit #"^/" ["1^/" "2"])

; Empty text is distinct from BLANK/null
(" A" = delimit ":" [_ "A" null])
(":A:" = delimit ":" ["" "A" ""])

; literal blanks act as spaces, fetched ones act as nulls
[
    ("a  c" = spaced ["a" _ comment <b> _ "c"])
    ("a c" = spaced ["a" blank comment <b> blank "c"])
    ("a c" = spaced ["a" null comment <b> null "c"])
]

; ISSUE! is to be merged with CHAR! and does not space
(
    project: 'Ren-C
    bad-thing: "Software Complexity"
    new?: does [project <> 'Rebol]

    str: spaced [#<< project #>> _ {The} (if new? 'NEW) {War On} bad-thing]

    str = "<<Ren-C>> The NEW War On Software Complexity"
)

; Empty blocks vaporize and do not add delimiters
;
("some**stuff" = delimit "**" [[] "some" [] "stuff" []])

; Empty strings do NOT vaporize, because DELIMIT needs to be able to point
; out empty fields.  Use NULL, BLANK!, or [] to convey true emptiness.
;
("**some****stuff**" = delimit "**" [{} "some" {} "stuff" {}])

; BLOCK! acts the same as if doing an APPEND to an empty TEXT!; e.g. items are
; not reduced and no spacing is introduced.  They actually run the same code
; path as APPEND of BLOCK! to TEXT!.
;
; GET-BLOCK! can be used when reducing is desired.
[
    (
        word: "part"
        "Mixing unspacedword with spaced part" = spaced [
            "Mixing" ["unspaced" word] "with" "spaced" word
        ]
    )(
        word: "part"
        "Mixing unspacedpart with spaced part" = spaced [
            "Mixing" :["unspaced" word] "with" "spaced" word
        ]
    )
]

; ~null~ and ~void~ isotopes are considered vaporizations
; other isotopes are errors
[
    ("Hello World" = spaced ["Hello" ~null~ "World"])
    ("Hello World" = spaced ["Hello" if false ["Cruel"] "World"])
    ("Hello World" = spaced compose ["Hello" (if false ["Cruel"]) "World"])

    ("HelloWorld" = unspaced ["Hello" ~void~ "World"])
    (
        f: make frame! :void
        "HelloWorld" = unspaced ["Hello" do f "World"]
    )

    (
        e: trap [spaced ["Hello" ~baddie~ "World"]]
        did all [
            e.id = 'bad-isotope
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

    (null = delimit/head/tail "," [null])
    (null = delimit/head/tail "," _)
]
