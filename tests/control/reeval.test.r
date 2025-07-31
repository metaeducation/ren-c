; %reeval.test.r
;
; REEVAL is a function which will re-run an expression as if it had been in
; the stream of evaluation to start with.

; REEVAL can invoke an ACTION! by value, thus taking the place of DO for this
; from historical Rebol.  Being devoted to this singular purpose of dispatch
; is better than trying to hook the more narrow DO primitive, as variadics
; are more difficult to wrap and give alternate behaviors to:
;
; https://forum.rebol.info/t/meet-the-reevaluate-reeval-native/311
[
    (1 = reeval :abs -1)
]

; REEVAL can handle other variadic cases
(
    x: 10
    reeval (first [x:]) 20
    x = 20
)

(
    a-value: charset ""
    same? a-value reeval a-value
)
(
    a-value: integer!
    same? a-value reeval a-value
)
(1/Jan/0000 = reeval 1/Jan/0000)
(0.0 = reeval 0.0)
(1.0 = reeval 1.0)
(
    a-value: me@here.com
    same? a-value reeval a-value
)
(
    a-value: does [5]
    5 = reeval :a-value
)
(
    a: 12
    a-value: first [:a]
    :a = reeval :a-value
)

(0 = reeval 0)
(1 = reeval 1)
(#a = reeval #a)

[#2101 #1434 (
    a-value: first ['a/b]
    all [
        lit-path? a-value
        path? reeval :a-value
        (as path! unquote :a-value) = (reeval :a-value)
    ]
)]

(
    a-value: first ['a]
    all [
        lit-word? a-value
        word? reeval :a-value
        (to-word unquote :a-value) = (reeval :a-value)
    ]
)

~expect-arg~ !! (reeval okay)
~expect-arg~ !! (reeval null)

(null? eval opt null)
(
    a-value: make object! []
    same? :a-value reeval :a-value
)
(
    a-value: 'a.b
    a: make object! [b: 1]
    1 = reeval :a-value
)
(
    a-value: make port! http://
    port? reeval :a-value
)
~need-non-end~ !! (
    a-value: first [a.b:]
    assert [chain? a-value]
    (reeval a-value)  ; no value to assign after it...
)
(
    a-value: to tag! ""
    same? a-value reeval a-value
)
(0:00 = reeval 0:00)
(0.0.0 = reeval 0.0.0)
(
    a-value: 'b-value
    b-value: 1
    1 = reeval :a-value
)

(integer? (reeval the (comment "this group vaporizes") 1020))

(<before> = (<before> reeval comment/ "erase me"))


; !!! There used to be some concept that GHOST!-returning things could
; appear like an "end" to functions.  But rules for reification have changed,
; in that there are no "pure invisibles".  So saying that it's an <end> is
; questionable.  Review when there's enough time in priorities to think on it.
;
;     (not error? rescue [reeval (lambda [x [<end>]] []) ||| 1 2 3])
;     (warning? rescue [reeval (lambda [x [null?]] []) ||| 1 2 3])

(
    x: <before>
    all [
        10 = (
            10 reeval elide/ x: <after>
        )
        x = <after>
    ]
)
