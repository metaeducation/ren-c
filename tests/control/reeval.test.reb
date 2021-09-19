; %reeval.test.reb
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
    (1 == reeval :abs -1)
]

; REEVAL can handle other variadic cases, such as SET-WORD!
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
    a-value: blank!
    same? a-value reeval a-value
)
(1/Jan/0000 == reeval 1/Jan/0000)
(0.0 == reeval 0.0)
(1.0 == reeval 1.0)
(
    a-value: me@here.com
    same? a-value reeval a-value
)
(
    a-value: does [5]
    5 == reeval :a-value
)
(
    a: 12
    a-value: first [:a]
    :a == reeval :a-value
)
(NUL == reeval NUL)
(
    a-value: make image! 0x0
    same? a-value reeval a-value
)
(0 == reeval 0)
(1 == reeval 1)
(#a == reeval #a)

[#2101 #1434 (
    a-value: first ['a/b]
    all [
        lit-path? a-value
        path? reeval :a-value
        (as path! unquote :a-value) == (reeval :a-value)
    ]
)]

(
    a-value: first ['a]
    all [
        lit-word? a-value
        word? reeval :a-value
        (to-word unquote :a-value) == (reeval :a-value)
    ]
)
(true = reeval true)
(false = reeval false)
($1 == reeval $1)
(null? reeval (specialize :of [property: 'type]) null)
(null? do _)
(
    a-value: make object! []
    same? :a-value reeval :a-value
)
(
    a-value: 'a.b
    a: make object! [b: 1]
    1 == reeval :a-value
)
(
    a-value: make port! http://
    port? reeval :a-value
)
(
    a-value: first [a/b:]
    all [
        set-path? :a-value
        error? trap [reeval :a-value]  ; no value to assign after it...
    ]
)
(
    a-value: make tag! ""
    same? :a-value reeval :a-value
)
(0:00 == reeval 0:00)
(0.0.0 == reeval 0.0.0)
(
    a-value: 'b-value
    b-value: 1
    1 == reeval :a-value
)
