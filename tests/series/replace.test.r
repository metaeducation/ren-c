; REPLACE tests, initial set from:
;
; File: %replace-test.red
; Author: "Nenad Rakocevic & Peter W A Wood & Toomas Vooglaid"
; Rights: "Copyright (C) 2011-2015 Red Foundation. All rights reserved."
; License: "BSD-3 - https://github.com/red/red/blob/master/BSD-3-License.txt"
;


; REPLACE

([1 2 8 4 5] = replace:one [1 2 3 4 5] 3 8)
([1 2 8 9 4 5] = replace:one [1 2 3 4 5] 3 spread [8 9])
([1 2 8 9 5] = replace:one [1 2 3 4 5] spread [3 4] spread [8 9])
([1 2 8 5] = replace:one [1 2 3 4 5] spread [3 4] 8)
([1 2 a 5] = replace:one [1 2 3 4 5] spread [3 4] spread [a])
([a [g] c d] = replace:one [a b c d] 'b [g])
([a g c d] = replace:one [a b c d] 'b spread [g])
(#{006400} = replace:one #{000100} #{01} 100)
(%file.ext = replace:one %file.sub.ext ".sub." #".")
("abra-abra" = replace:one "abracadabra" "cad" #"-")
("abra-c-adabra" = replace:one "abracadabra" #"c" "-c-")

([<ice> 1 <ice> <baby>] = replace [a 1 a <baby>] word?/ <ice>)

; Red has a bizarre behavior for this which is in their tests:
; https://github.com/red/red/blob/12ad56be0fc474f7738c0ef891725e49f9738010/tests/source/units/replace-test.red#L24
;
;    ('a/b/c/h/i/d/e = replace 'a/b/g/h/i/d/e 'g/h/i 'c)
;
; That's not what Rebol2 does, and not consistent with BLOCK! replacement
; Ren-C doesn't allow REPLACE on paths in any case--they are immutable, one
; must convert to a block and back (the conversion may fail as not all blocks
; can become paths)

; REPLACE ALL

([1 4 3 4 5] = replace [1 2 3 2 5] 2 4)
([1 4 5 3 4 5] = replace [1 2 3 2] 2 spread [4 5])
([1 8 9 8 9] = replace [1 2 3 2 3] spread [2 3] spread [8 9])
([1 8 8] = replace [1 2 3 2 3] spread [2 3] 8)
(#{640164} = replace #{000100} #{00} #{64})
(%file.sub.ext = replace %file!sub!ext #"!" #".")
(<tag body end> = replace <tag_body_end> "_" " ")

; REPLACE:CASE

("axbAab" = replace:case:one "aAbAab" "A" "x")
("axbxab" = replace:case "aAbAab" "A" "x")
("axbAab" = replace:case:one "aAbAab" "A" does ["x"])
("axbxab" = replace:case "aAbAab" "A" does ["x"])
(%file.txt = replace:case:one %file.TXT.txt %.TXT "")
(%file.txt = replace:case %file.TXT.txt.TXT %.TXT "")
(<tag xyXx> = replace:case:one <tag xXXx> "X" "y")
(<tag xyyx> = replace:case <tag xXXx> "X" "y")
(["a" "B" "x"] = replace:case:one ["a" "B" "a" "b"] spread ["a" "b"] "x")
('(x A x) = replace:case '(a A a) spread [a] spread [x])

;((make hash! [x a b [a B]]) = replace:case make hash! [a B a b [a B]] [a B] 'x)

; REPLACE with VOID

(null? replace:one copy [3 0 4] 0 ())
(null? replace:one copy [3 0 4] comment "hi" 1020)
(null? replace copy [1 0 2 0 1 0 2 0] spread [1 0] ())
(null? replace:one copy "304" "0" ^ghost)
(null? replace:one copy "304" () "1020")
(null? replace copy "10201020" "10" ())
(null? replace:one copy #{300040} #{00} ~,~)
(null? replace:one copy #{300040} ~()~ #{10002000})
(null? replace copy #{1000200010002000} #{1000} nihil)

; A function that returns void opts out of replacements, not the whole replace.
;
(#{1000200010002000} = replace copy #{1000200010002000} #{1000} does [])

; REPLACE with NONE

([3 4] = replace:one copy [3 0 4] 0 ~[]~)
([3 0 4] = replace:one copy [3 0 4] none 1020)
([2 0 2 0] = replace copy [1 0 2 0 1 0 2 0] spread [1 0] none)
("34" = replace:one copy "304" "0" spread [])
("304" = replace:one copy "304" ~[]~ "1020")
("2020" = replace copy "10201020" "10" ~[]~)
(#{3040} = replace:one copy #{300040} #{00} none)
(#{300040} = replace:one copy #{300040} none #{10002000})
(#{20002000} = replace copy #{1000200010002000} #{1000} spread [])

; A function that returns NONE each time is the same as passing NONE as the
; overall replacement.
;
(#{20002000} = replace copy #{1000200010002000} #{1000} does [none])

; REPLACE:DEEP ONE - :DEEP not (yet?) implemented in Ren-C

;([1 2 3 [8 5]] = replace:one:deep [1 2 3 [4 5]] [just 4] 8)
;([1 2 3 [4 8 9]] = replace:one:deep [1 2 3 [4 5]] [just 5] [8 9])
;([1 2 3 [8 9]] = replace:one:deep [1 2 3 [4 5]] [just 4 just 5] [8 9])
;([1 2 3 [8]] = replace:one:deep [1 2 3 [4 5]] [just 4 just 5] 8)
;([1 2 a 4 5] = replace:one:deep [1 2 3 4 5] [just 8 | just 4 | just 3] 'a)
;([a g h c d] = replace:one:deep [a b c d] ['b | 'd] [g h])

; REPLACE:DEEP ALL - :DEEP not (yet?) implemented in Ren-C

;([1 8 3 [4 8]] = replace:deep [1 2 3 [4 2]] [just 2] 8)
;([1 8 9 3 [4 8 9]] = replace:deep [1 2 3 [4 5]] [just 5 | just 2] [8 9])
;([i j c [i j]] = replace:deep [a b c [d e]] ['d 'e | 'a 'b] [i j])
;([a [<tag> [<tag>]]] = replace:deep [a [b c [d b]]] ['d 'b | 'b 'c] <tag>)

; REPLACE:CASE:DEEP - :DEEP not (yet?) implemented in Ren-C

;([x A x B [x A x B]] = replace:case:deep [a A b B [a A b B]] ['a | 'b] 'x)
;((just (x A x B (x A x B))) = replace:case:deep lit (a A b B (a A b B)) ['a | 'b] 'x)

[
    https://github.com/metaeducation/rebol-issues/issues/1499

    ("ac" = replace:one "abc" "bc" "c")
    (#{6163} = replace:one #{616263} #{6263} #{63})
]
