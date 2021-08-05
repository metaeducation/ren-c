; %parse-skip.test.reb
;
; SKIP is reimagined as invisible; so <any> should be used to capture a single
; "anything" value...

(<a> == uparse [<a> 10] [tag! skip])  ; why be 10 if it was "skipped"
