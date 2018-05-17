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

; Nested unspaced
(
    nested-unspaced: func [n] [
        either n <= 1 [n] [unspaced [n space nested-unspaced n - 1]]
    ]
    "9 8 7 6 5 4 3 2 1" = nested-unspaced 9
)
; Form recursive object...
(
    o: object [a: 1 r: _] o/r: o
    (unspaced ["<" form o  ">"]) = "<a: 1^/r: make object! [...]>"
)
; detab...
(
    (unspaced ["<" detab "aa^-b^-c" ">"]) = "<aa  b   c>"
)
; entab...
(
    (unspaced ["<" entab "     a    b" ">"]) = "<^- a    b>"
)
; dehex...
(
    (unspaced ["<" dehex "a%20b" ">"]) = "<a b>"
)
; form...
(
    (unspaced ["<" form [1 <a> [2 3] "^""] ">"]) = {<1 <a> 2 3 ">}
)
; transcode...
(
    (unspaced ["<" mold transcode to binary! "a [b c]"  ">"])
        = "<[a [b c] #{}]>"
)
; ...
(
    (unspaced ["<" intersect [a b c] [d e f]  ">"]) = "<>"
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


; !!! HELP and SOURCE have become more complex in Ren-C, due to the appearance
; of function compositions, and an attempt to have their "meta" information
; guide the help system--to try and explain their relationships, while not
; duplicating unnecessary information.  e.g. a function with 20 parameters
; that is specialized to have one parameter fixed, should not need all
; other 19 parameter descriptions to be copied onto a new function.
;
; The code trying to sort out these relationships has come along organically,
; and involves a number of core questions--such as when to use null vs. blank.
; It has had a tendency to break, so these tests are here even though they
; spew a large amount of output, in the interests of making HELP stay working.
;
for-each w words of lib [if action? get w [help w] else [help (get w)]]
for-each w words of lib [if action? get w [source (w)]]
