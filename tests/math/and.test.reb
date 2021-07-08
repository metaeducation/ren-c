; %and.test.reb
;
; Historical Rebol's NOT was "conditional" (tested for conditional truth
; or falsehood using the same rules as IF and other conditions, and returned
; #[true] or #[false]).  However the other logic words like AND, OR, and XOR
; were handled as bitwise operations...that could also be used to intersect
; or union sets of values.
;
; Changing this convention was a popular suggestion:
; https://github.com/metaeducation/rebol-issues/issues/1879
;

; logic!
(true and+ true = true)
(true and+ false = false)
(false and+ true = false)
(false and+ false = false)

; integer!
(1 and+ 1 = 1)
(1 and+ 0 = 0)
(0 and+ 1 = 0)
(0 and+ 0 = 0)
(1 and+ 2 = 0)
(2 and+ 1 = 0)
(2 and+ 2 = 2)

; char!
(NUL and+ NUL = NUL)
(#"^(01)" and+ NUL = NUL)
(NUL and+ #"^(01)" = NUL)
(#"^(01)" and+ #"^(01)" = #"^(01)")
(#"^(01)" and+ #"^(02)" = NUL)
(#"^(02)" and+ #"^(02)" = #"^(02)")

; tuple!
(0.0.0 and+ 0.0.0 = 0.0.0)
(1.0.0 and+ 1.0.0 = 1.0.0)
(2.0.0 and+ 2.0.0 = 2.0.0)
(255.255.255 and+ 255.255.255 = 255.255.255)

; binary!
(#{030000} and+ #{020000} = #{020000})

; !!! arccosing tests that somehow are in and.test.reb
(0 = arccosine 1)
(0 = arccosine/radians 1)
(30 = arccosine (square-root 3) / 2)
((pi / 6) = arccosine/radians (square-root 3) / 2)
(45 = arccosine (square-root 2) / 2)
((pi / 4) = arccosine/radians (square-root 2) / 2)
(60 = arccosine 0.5)
((pi / 3) = arccosine/radians 0.5)
(90 = arccosine 0)
((pi / 2) = arccosine/radians 0)
(180 = arccosine -1)
(pi = arccosine/radians -1)
(150 = arccosine (square-root 3) / -2)
(((pi * 5) / 6) = arccosine/radians (square-root 3) / -2)
(135 = arccosine (square-root 2) / -2)
(((pi * 3) / 4) = arccosine/radians (square-root 2) / -2)
(120 = arccosine -0.5)
(((pi * 2) / 3) = arccosine/radians -0.5)
(error? trap [arccosine 1.1])
(error? trap [arccosine -1.1])


; GROUP! for the right clause, short circuit.
;
(false and (false) = false)
(false and (true) = false)
(true and (false) = false)
(true and (true) = true)
(
    x: 1020
    did all [
        true and (x: _) = false
        x = _
    ]
)
(
    x: _
    did all [
        true and (x: 304) = true
        x = 304
    ]
)
(
    x: 1020
    did all [
        <truthy> and (x: 304) = true
        x = 304
    ]
)
(
    x: 1020
    did all [
        <truthy> and (x: _) = false
        x = _
    ]
)


(false or (false) = false)
(false or (true) = true)
(true or (false) = true)
(true or (true) = true)
(
    x: 1020
    did all [
        false or (x: _) = false
        x = _
    ]
)
(
    x: _
    did all [
        false or (x: 304) = true
        x = 304
    ]
)
(
    x: 1020
    did all [
        _ or (x: 304) = true
        x = 304
    ]
)
(
    x: 1020
    did all [
        _ or (x: true) = true
        x = true
    ]
)


; WORD! and PATH! are allowed as the right hand side of AND/OR, as
; a synonym for that word or path in a GROUP.

; !!! Once this allowed running functions.  Currently it does not.  There
; are some problems with that idea, e.g. if they take <skip> arguments or
; are enfix...so ruling them out is the most conervative choice.

[
    (
        x: 1
        y: true  ; truesum: does [x: x * 2 true]
        n: false  ; falsesum: does [x: x * 3 false]
        o: make object! [
            y: true  ; :truesum
            n: false  ; :falsesum
        ]
        true
    )

    (did y and y)
    (not y and n)
    (not n and y)
    (not n and n)
    ;(x = 216)

    (did o/y and o/y)
    (not o/y and o/n)
    (not o/n and o/y)
    (not o/n and o/n)
    ;(216 * 216 = x)

    (did y or y)
    (did y or n)
    (did n or y)
    (not n or n)
    ;(216 * 216 * 216 = x)

    (did o/y or o/y)
    (did o/y or o/n)
    (did o/n or o/y)
    (not o/n or o/n)
    ;(216 * 216 * 216 * 216 = x)
]
