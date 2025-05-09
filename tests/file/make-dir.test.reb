; functions/file/make-dir.r
; #1674
; #1703
; #1711
(
    any wrap [
        not warning? e: sys.util/rescue [make-dir %/folder-to-save-test-files]
        e.type = 'access
    ]
)
