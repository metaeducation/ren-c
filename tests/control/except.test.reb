; %except.test.reb
;
; Like CATCH from some other languages, as an enfix operation in the
; spirit of THEN/ELSE which reacts to failures.  (THEN and ELSE do not,
; passing any failures along, which will raise an alarm if not
; consumed by some ^META receiving site.
;
; Note: "failures" are like the isotope form of ERROR!.  They cannot
; be stored in variables.

(
   x: ~
   did all [
       #X = fail "hello" then [x: #T] else [x: #E] except e -> [x: e, #X]
       error? x
   ]
)
(
    e: trap [
        fail "foo" then [print "THEN"] else [print "ELSE"]
    ]
    e.message = "foo"
)
(
    void? (void except e -> [<unused>])
)
