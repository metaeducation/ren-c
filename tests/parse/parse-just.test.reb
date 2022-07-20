; %parse-just.test.reb
;
; !!! The purpose this was designed for has gone away.  It will likely die.


((the 'a) = parse [] [just* a])
(['a] = parse [] [collect [keep just* a]])
