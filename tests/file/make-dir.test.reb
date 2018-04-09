; functions/file/make-dir.r
; #1674
; #1703
; #1711
(
    any [
        not error? e: try [make-dir %/folder-to-save-test-files]
        e/type = 'access
    ]
)
