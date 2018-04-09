; functions/file/open.r
[#1422 ; "Rebol crashes when opening the 128th port"
    (
    error? try [
        repeat n 200 [
            try [close open open join-of tcp://localhost: n]
        ]
    ]
    true
    )
]
