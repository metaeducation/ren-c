; REPLACE tests, initial set from:
;
; File: %replace-test.red
; Author: "Nenad Rakocevic & Peter W A Wood & Toomas Vooglaid"
; Rights: "Copyright (C) 2011-2015 Red Foundation. All rights reserved."
; License: "BSD-3 - https://github.com/red/red/blob/master/BSD-3-License.txt"
;


; REPLACE

([1 2 8 4 5] = replace [1 2 3 4 5] 3 [8])
([1 2 8 9 4 5] = replace [1 2 3 4 5] 3 [8 9])
([1 2 8 9 5] = replace [1 2 3 4 5] [3 4] [8 9])
([1 2 8 5] = replace [1 2 3 4 5] [3 4] [8])
([1 2 a 5] = replace [1 2 3 4 5] [3 4] [a])
([a g c d] = replace [a b c d] 'b [g])
(#{006400} = replace #{000100} #{01} 100)
(%file.ext = replace %file.sub.ext ".sub." #".")
("abra-abra" = replace "abracadabra" "cad" #"-")
("abra-c-adabra" = replace "abracadabra" #"c" "-c-")

; Red has a bizarre behavior for this which is in their tests:
; https://github.com/red/red/blob/12ad56be0fc474f7738c0ef891725e49f9738010/tests/source/units/replace-test.red#L24
;
;    ('a/b/c/h/i/d/e = replace 'a/b/g/h/i/d/e 'g/h/i 'c)
;
; That's not what Rebol2 does, and not consistent with BLOCK! replacement
; Ren-C doesn't allow REPLACE on paths in any case--they are immutable, one
; must convert to a block and back (the conversion may fail as not all blocks
; can become paths)

; REPLACE/ALL

([1 4 3 4 5] = replace/all [1 2 3 2 5] 2 [4])
([1 4 5 3 4 5] = replace/all [1 2 3 2] 2 [4 5])
([1 8 9 8 9] = replace/all [1 2 3 2 3] [2 3] [8 9])
([1 8 8] = replace/all [1 2 3 2 3] [2 3] [8])
(#{640164} = replace/all #{000100} #{00} #{64})
(%file.sub.ext = replace/all %file!sub!ext #"!" #".")
(<tag body end> = replace/all <tag_body_end> "_" " ")

; REPLACE/CASE

("axbAab" = replace/case "aAbAab" "A" "x")
("axbxab" = replace/case/all "aAbAab" "A" "x")
("axbAab" = replace/case "aAbAab" ["A"] does ["x"])
("axbxab" = replace/case/all "aAbAab" ["A"] does ["x"])
(%file.txt = replace/case %file.TXT.txt %.TXT "")
(%file.txt = replace/case/all %file.TXT.txt.TXT %.TXT "")
(<tag xyXx> = replace/case <tag xXXx> "X" "y")
(<tag xyyx> = replace/case/all <tag xXXx> "X" "y")
(["a" "B" "x"] = replace/case/all ["a" "B" "a" "b"] ["a" "b"] ["x"])
((lit (x A x)) = replace/case/all lit (a A a) 'a [x])

;((make hash! [x a b [a B]]) = replace/case make hash! [a B a b [a B]] [a B] 'x)

; REPLACE null behavior (Ren-C only, as only it has null)
; https://forum.rebol.info/t/null-first-class-values-and-safety/895/2

([3 4] = replace copy [3 0 4] 0 null)
([3 0 4] = replace copy [3 0 4] null 1020)
([2 0 2 0] = replace/all copy [1 0 2 0 1 0 2 0] [1 0] null)
("34" = replace copy "304" "0" null)
("304" = replace copy "304" null "1020")
("2020" = replace/all copy "10201020" "10" null)
(#{3040} = replace copy #{300040} #{00} null)
(#{300040} = replace copy #{300040} null #{10002000})
(#{20002000} = replace/all copy #{1000200010002000} #{1000} null)

; REPLACE/DEEP - /DEEP not (yet?) implemented in Ren-C

;([1 2 3 [8 5]] = replace/deep [1 2 3 [4 5]] [lit 4] 8)
;([1 2 3 [4 8 9]] = replace/deep [1 2 3 [4 5]] [lit 5] [8 9])
;([1 2 3 [8 9]] = replace/deep [1 2 3 [4 5]] [lit 4 lit 5] [8 9])
;([1 2 3 [8]] = replace/deep [1 2 3 [4 5]] [lit 4 lit 5] 8)
;([1 2 a 4 5] = replace/deep [1 2 3 4 5] [lit 8 | lit 4 | lit 3] 'a)
;([a g h c d] = replace/deep [a b c d] ['b | 'd] [g h])

; REPLACE/DEEP/ALL - /DEEP not (yet?) implemented in Ren-C

;([1 8 3 [4 8]] = replace/deep/all [1 2 3 [4 2]] [lit 2] 8)
;([1 8 9 3 [4 8 9]] = replace/deep/all [1 2 3 [4 5]] [lit 5 | lit 2] [8 9])
;([i j c [i j]] = replace/deep/all [a b c [d e]] ['d 'e | 'a 'b] [i j])
;([a [<tag> [<tag>]]] = replace/all/deep [a [b c [d b]]] ['d 'b | 'b 'c] <tag>)

; REPLACE IN STRING WITH RULE
;
; !!! This is a Red invention that seems like a pretty bad idea.  It mixes
; PARSE mechanisms in with REPLACE, but it only works if the source data is
; a string...because it "knows" not to treat the block string-like.  This is
; in contention with many other functions that assume you want to stringify
; blocks when passed, and block parsing can't use the same idea.  If anything
; it sounds like a PARSE feature.
;
;("!racadabra" = replace "abracadabra" ["ra" | "ab"] #"!")
;("!!cad!!" = replace/all "abracadabra" ["ra" | "ab"] #"!")
;("!!cad!!" = replace/all "abracadabra" ["ra" | "ab"] does ["!"])
;("AbrACAdAbrA" == replace/all "abracadabra" [s: ["a" | "c"]] does [uppercase s/1])
;("a-babAA-" = replace/case/all "aAbbabAAAa" ["Ab" | "Aa"] "-")

; REPLACE/CASE/DEEP - /DEEP not (yet?) implemented in Ren-C

;([x A x B [x A x B]] = replace/case/deep/all [a A b B [a A b B]] ['a | 'b] 'x)
;((lit (x A x B (x A x B))) = replace/case/deep/all lit (a A b B (a A b B)) ['a | 'b] 'x)
