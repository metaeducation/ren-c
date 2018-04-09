; system/gc.r
[#1776 #2072 (
    a: copy []
    loop 200'000 [a: append/only copy [] a]
    recycle
    true
)]
[#1989 (
    loop ([comment 30000000] 300) [make gob! []]
    true
)]

; !!! simplest possible LOAD/SAVE smoke test, expand!
(
    file: %simple-save-test.r
    data: "Simple save test produced by %core-tests.r"
    save file data
    (load file) = data
)


;;
;; "Mold Stack" tests
;;

; Nested ajoin
(
    nested-ajoin: func [n] [
        either n <= 1 [n] [ajoin [n space nested-ajoin n - 1]]
    ]
    "9 8 7 6 5 4 3 2 1" = nested-ajoin 9
)
; Form recursive object...
(
    o: object [a: 1 r: _] o/r: o
    (ajoin ["<" form o  ">"]) = "<a: 1^/r: make object! [...]>"
)
; detab...
(
    (ajoin ["<" detab "aa^-b^-c" ">"]) = "<aa  b   c>"
)
; entab...
(
    (ajoin ["<" entab "     a    b" ">"]) = "<^- a    b>"
)
; dehex...
(
    (ajoin ["<" dehex "a%20b" ">"]) = "<a b>"
)
; form...
(
    (ajoin ["<" form [1 <a> [2 3] "^""] ">"]) = {<1 <a> 2 3 ">}
)
; transcode...
(
    (ajoin ["<" mold transcode to binary! "a [b c]"  ">"])
        = "<[a [b c] #{}]>"
)
; ...
(
    (ajoin ["<" intersect [a b c] [d e f]  ">"]) = "<>"
)
; reword
(equal? reword "$1 is $2." [1 "This" 2 "that"] "This is that.")
(equal? reword/escape "A %%a is %%b." [a "fox" b "brown"] "%%" "A fox is brown." )
(equal? reword/escape "I am answering you." ["I am" "Brian is" you "Adrian"] blank "Brian is answering Adrian.")
(equal? reword/escape "$$$a$$$ is $$$b$$$" [a Hello b Goodbye] ["$$$" "$$$"] "Hello is Goodbye")


;;
;; Simplest possible HTTP and HTTPS protocol smoke test
;;
;; !!! EXPAND!
;;

[#1613 (
    ; !!! Note that returning a WORD! from a function ending in ? is not seen
    ; as a good practice, and will likely change.
    ;
    'file = exists? http://www.rebol.com/index.html
)]

(not error? trap [read http://example.com])
(not error? trap [read https://example.com])
