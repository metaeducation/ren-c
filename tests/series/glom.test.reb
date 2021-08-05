; %glom.test.reb
;
; Optimized operation for accumulation.

(_ = glom _ _)
([a] = glom copy [a] _)
([a [b c]] = glom copy [a] ^(copy [b c]))
([a b c] = glom copy [a] copy [b c])
