; functions/file/open.r
[#1422 (  ; "Rebol crashes when opening the 128th port"
    count-up n 200 [
        close open join tcp://localhost: n
    ]
    true
)]
