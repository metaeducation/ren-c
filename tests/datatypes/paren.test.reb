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
    fact: copy quote (
        either num1 = 1 [num2] [num2: num1 * num2 num1: num1 - 1]
    )
    insert/only tail of last fact fact
    24 = do fact
)
; infinite recursion
[#1665 (
    fact: to group! []
    insert/only fact fact
    error? trap [do fact]
)]
