; %glom.test.reb
;
; Optimized operation for accumulation.

(null = glom null null)
([a] = glom copy [a] null)
([a [b c]] = glom copy [a] ^(copy [b c]))
([a b c] = glom copy [a] copy [b c])
