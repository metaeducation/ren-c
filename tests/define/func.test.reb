; functions/define/func.r
; recursive safety
(
    f: func [return: [action!]] [
        func [x] [
            either x = 1 [
                reeval f 2
                x = 1
            ][
                null
            ]
        ]
    ]
    reeval f 1
)
