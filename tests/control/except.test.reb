; %except.test.reb
;
; Like CATCH from some other languages, as an infix operation in the
; spirit of THEN/ELSE which reacts to failures.  (THEN and ELSE do not,
; passing any failures along, which will raise an alarm if not
; consumed by some ^META receiving site.
;
; Note: "raised" is the antiform of ERROR!.  Can't be stored in variables.

(
   x: ~
   all [
       #X = raise "hello" then [x: #T] else [x: #E] except e -> [x: e, #X]
       error? x
   ]
)
(
    e: sys.util/rescue [
        panic "foo" then [print "THEN"] else [print "ELSE"]
    ]
    e.message = "foo"
)
(
    void? (void except e -> [<unused>])
)
