; %glom.test.r
;
; Optimized operation for accumulation.

(~()~ = glom ~()~ ^void)
([a] = glom copy [a] ^void)
([a [b c]] = glom copy [a] copy [b c])
([a b c] = glom copy [a] spread copy [b c])
