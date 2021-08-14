; %parse-the-xxx.test.reb
;
; @ and the @xxx types are used to synthesize material out of thin air without using
; a GROUP!.  This becomes important if you are writing COMPOSE rules, because if
; you have to use a GROUP! every time you fabricate material that isn't being matched
; in the input then that is contentious with the use of GROUP!s to call out places
; in the composition.  @ breaks the Gordian knot in such a situation by just taking
; whatever follows it literally.

([keep some #a] = uparse "a" [collect [keep @[keep some], keep <any>]])
([keep #a] = uparse "a" [collect [keep @ 'keep, keep <any>]])
([keep #a] = uparse "a" [collect [keep the 'keep, keep <any>]])

([keep #a] = uparse "a" [collect [keep ^ @keep, keep <any>]])
([tupley.keep #a] = uparse "a" [collect [keep ^ @tupley.keep, keep <any>]])
([keep.pathy #a] = uparse "a" [collect [keep ^ @keep/pathy, keep <any>]])

([(keep some) #a] = uparse "a" [collect [keep ^ @(keep some), keep <any>]])
