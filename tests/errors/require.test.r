; %require.test.r
;
; Promotes FAIL to PANIC, e.g. as precaution before a LIFT or RETURN

(all [
    not sys.util/recover [lift fail "some error"]
    did sys.util/recover [lift require fail "some error"]
])
