; %except.test.r
;
; Like CATCH from some other languages, as an infix operation in the
; spirit of THEN/ELSE which reacts to failures.  (THEN and ELSE do not,
; passing any failures along, which will raise an alarm if not
; consumed by some ^META receiving site.)
;
; Note: errors are unstable antiforms and cn't be stored in variables.

(
   x: ~
   all [
       #X = fail "hello" then [x: #T] else [x: #E] except e -> [x: e, #X]
       warning? x
   ]
)
(
    e: sys.util/recover [
        panic "foo" then [print "THEN"] else [print "ELSE"]
    ]
    e.message = "foo"
)
(
    void? (void except e -> [<unused>])
)
