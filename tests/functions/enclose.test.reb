; better-than-nothing ENCLOSE tests

(
    e-multiply: enclose 'multiply function [f [frame!]] [
        diff: abs (f/value1 - f/value2)
        result: do f
        return result + diff
    ]

    73 = e-multiply 7 10
)
(
    n-add: enclose 'add function [f [frame!]] [
        if 10 = f/value1 [return blank]
        f/value1: 5
        do f
    ]

    did all [
        blank? n-add 10 20
        25 = n-add 20 20
    ]
)
