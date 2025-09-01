; %typecheck.test.r

(typecheck [integer!] 1020)
(not typecheck [integer!] <foo>)

; If you don't want decay you have to use :META
;
~???~ !! (typecheck [void?] ^void)
~???~ !! (typecheck [integer!] ^void)
(typecheck:meta [void?] ^void)
(not typecheck:meta [integer!] ^void)

; ERROR! will be decayed by default, can't pass test
;
~zero-divide~ !! (typecheck [integer!] 1 / 0)
~zero-divide~ !! (typecheck [error!] 1 / 0)

; With ^META ERROR! can be used with the test
;
(typecheck:meta [error!] 1 / 0)

; But a ^META typecheck where ERROR! doesn't pass, passes it through
; (this is strictly more powerful than a :RELAX refinement would be, as TRY
; gives you the equivalent to what that would do)
;
((typecheck:meta [integer!] 1 / 0) except e -> [e.id = 'zero-divide])
(not try typecheck:meta [integer!] 1 / 0)
