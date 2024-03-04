; functions/file/open.r
[#1422 ; "Rebol crashes when opening the 128th port"
    (
    error? trap [
        count-up n 200 [
            trap [close open open join tcp://localhost: n]
        ]
    ]
    true
    )
]
