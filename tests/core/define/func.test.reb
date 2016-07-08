; functions/define/func.r
; recursive safety
[
    f: func [] [
        func [x] [
            if x = 1 [
                eval f 2
                x = 1
            ]
        ]
    ]
    eval f 1
]
