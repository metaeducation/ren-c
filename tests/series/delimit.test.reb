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

; Empty moldings vaporize and do not add delimiters.
;
("some**stuff" = delimit "**" [{} "some" [] "stuff" {}])
("some**stuff" = delimit "**" [[] "some" {} "stuff" []])
