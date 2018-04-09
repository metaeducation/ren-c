; functions/define/func.r
; recursive safety
(
    f: func [return: [function!]] [
        func [x] [
            either x = 1 [
                eval f 2
                x = 1
            ][
                false
            ]
        ]
    ]
    eval f 1
)
