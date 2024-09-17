; functions/file/open.r
[#1422 ; "Rebol crashes when opening the 128th port"
    (
    error? sys/util/rescue [
        count-up n 200 [
            sys/util/rescue [close open open join tcp://localhost: n]
        ]
    ]
    true
    )
]
