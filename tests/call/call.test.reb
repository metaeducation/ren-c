; call/call.test.reb

;; CALL/OUTPUT tests
;; see - https://github.com/metaeducation/ren-c/issues/537
;; and following fixes
;; - https://github.com/metaeducation/ren-c/commit/298409f485420ecd03f0be4b465111be4ad829cd
;; - https://github.com/metaeducation/ren-c/commit/e57c147465f3ed47f297e7a3ce3bb0319635f81f

(
    ; small - note Windows doesn't do BLOCK! arg to CALL (argv style) yet

    call/shell/wait/output spaced [
        (file-to-local system/options/boot)
        {--suppress "*" call/print.reb 100}
    ] data: copy {}

    length of data = 100
)
(
    ; medium - note Windows doesn't do BLOCK! arg to CALL (argv style) yet

    call/shell/wait/output spaced [
        (file-to-local system/options/boot)
        {--suppress "*" call/print.reb 9000}
    ] data: copy {}

    length of data = 9000
)
(
    ; large - note Windows doesn't do BLOCK! arg to CALL (argv style) yet

    call/shell/wait/output spaced [
        (file-to-local system/options/boot)
        {--suppress "*" call/print.reb 80000}
    ] data: copy {}

    length of data = 80'000
)

;; git log crash (inconsistent)
;; fixed by https://github.com/metaeducation/ren-c/commit/c2221bffa2815dd074dc00080e1a29816ad7f5e2
(
    ;; only going to run if can find git binary
    if exists? %/usr/bin/git [
        ;; extra large (500K+)
        data: copy {}
        call/wait/output [%/usr/bin/git "log" {--pretty=format:'[commit: {%h} author: {%an} email: {%ae} date-string: {%ai} summary: {%s}]'}] data
        length of data > 500'000 and (find data "summary: {Initial commit}]")
        ;; bottom of log
    ] else [true] ;; test wasn't run but no way to skip :(
)
