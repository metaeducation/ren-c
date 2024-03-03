
(
    file: ~
    all [
        %./ = split-path/file %./ 'file
        file = null
    ]
)
(
    file: ~
    all [
        %../ = split-path/file %../ 'file
        file = null
    ]
)
(
    file: ~
    all [
        %./ = split-path/file %test 'file
        file = %test
    ]
)
(
    file: ~
    all [
        %./ = split-path/file %test/ 'file
        file = %test/
    ]
)
(
    file: ~
    all [
        %test/ = split-path/file %test/test/ 'file
        file = %test/
    ]
)
