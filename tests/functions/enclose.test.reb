; better-than-nothing ENCLOSE tests

(
    e-multiply: enclose 'multiply function [f [frame!]] [
        diff: abs (f/value1 - f/value2)
        result: eval f
        return result + diff
    ]

    73 = e-multiply 7 10
)
(
    n-add: enclose 'add function [f [frame!]] [
        if 10 = f/value1 [return blank]
        f/value1: 5
        return eval f
    ]

    all [
        blank? n-add 10 20
        25 = n-add 20 20
    ]
)
