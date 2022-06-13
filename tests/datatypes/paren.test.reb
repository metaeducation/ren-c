; datatypes/paren.r
(group? first [(1 + 1)])
(not group? 1)
; minimum
(group! = type of first [()])
; alternative literal form
(strict-equal? first [()] first [#[group! [[] 1]]])
(strict-equal? first [()] make group! 0)
(strict-equal? first [()] to group! [])
("()" == mold first [()])
; parens are active
(
    a-value: first [(1)]
    1 == do reduce [:a-value]
)
; finite recursion
(
    num1: 4
    num2: 1
    fact: copy/deep the (
        either num1 = 1 [num2] [num2: num1 * num2 num1: num1 - 1]
    )
    insert/only tail of last fact fact
    24 = eval fact
)

; infinite recursion
; Ren-C has "stackless" recursion of GROUP! execution, so it can go to high
; numbers without exceeding the underlying system stack (e.g. the "C stack")
; (this means stack overflow errors vs. infinite loops or out of memory
; errors is a policy decision, which could utilize a counter the user sets)
;
[#859 #1665 (
    n: 0
    fact: to group! [n: n + 1 if n = 10000 [throw <done>]]
    append/only fact fact
    <done> = catch [eval fact]
)]
