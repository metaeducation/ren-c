; functions/series/sort.r
([1 2 3] == sort [1 3 2])
([3 2 1] == sort/reverse [1 3 2])
[#1152 ; SORT not stable (order not preserved)
    (["A" "a"] == sort ["A" "a"])
    (["A" "a"] == sort/reverse ["A" "a"])
    (["a" "A"] == sort ["a" "A"])
    (["A" "a"] == sort/case ["a" "A"])
    (["A" "a"] == sort/case ["A" "a"])

    (
    set [c d] sort reduce [a: "a" b: "a"]
    all [
        same? c a
        same? d b
        not same? c b
        not same? d a
    ]
    )

    ([1 9 1 5 1 7] == sort/skip/compare [1 9 1 5 1 7] 2 1)
]
([1 2 3] == sort/compare [1 3 2] :<)
([3 2 1] == sort/compare [1 3 2] :>)
[#1516 ; SORT/compare ignores the typespec of its function argument
    (error? trap [sort/compare reduce [1 2 _] :>])
]
