; %meta.test.reb
;
; Operator whose evaluator behavior is like QUOTE, but it distinguishes
; the internal NULL isotope state from plain NULL (which quote does not)

((the '3) = ^ 1 + 2)

('~null~ = ^ if true [null])

(null = ^ null)

; The ^ does not subvert normal "right hand side evaluation rules", and
; as such it skips invisibles, vs. giving back ~void~.  Use the ^(...) in order
; to detect invisibility.

((the '3) = ^ comment "Hi" 1 + 2)

; !!! Is this the best behavior, or should it say "need-non-end"?
;
(null = trap [^])
