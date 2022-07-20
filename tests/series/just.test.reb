; %just.test.reb
;
; !!! JUST was invented for a purpose it is no longer needed for, it may die.

((the 'x) = just* x)
((the '[a b c]) = just* [a b c])

([a b c 'd] = append [a b c] just* d)
([a b c ''d] = append [a b c] just* 'd)
([a b c '[d e]] = append [a b c] just* [d e])
([a b c ''[d e]] = append [a b c] just* '[d e])
