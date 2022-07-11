; %parse-just.test.reb
;
; JUST is added for generating a value and not moving the parse position, in
; particular as an assist to generative code which doesn't want to have to
; generate a GROUP! and quotes/blocks for that purpose.
;
;     parse data [... keep ([thing]) ...]
;     ; vs
;     parse data [... keep just thing ...]
;
; The second case is easier to generate, when you have [var: 'thing].  Because
; you wind up in a situation like `compose [... keep ???? ...]`, and it takes
; some ofuscating code to get it to work.
;
; This doesn't imply that casual rule authors would necessarily prefer the
; "naked" argument to JUST.  But it offers another option for those writing
; complex generators.


((the 'a) = parse [] [just a])
([a] = parse [] [collect [keep just a]])
