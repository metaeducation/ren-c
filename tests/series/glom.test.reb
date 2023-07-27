; %glom.test.reb
;
; Optimized operation for accumulation.

(null = glom null void)
([a] = glom copy [a] void)
([a [b c]] = glom copy [a] copy [b c])
([a b c] = glom copy [a] spread copy [b c])
