; %glom.test.r
;
; Optimized operation for accumulation.

(~()~ = glom ~()~ ~()~)

([a] = glom [a] ~()~)
([a] = glom ~()~ [a] )

([a [b c]] = glom [a] quote [b c])
([a b c] = glom [a] [b c])

([[b c] a] = glom quote [b c] [a] )
([b c a] = glom [b c] [a] )

(
    var: [a b c]
    [a b c d e f] = glom $var [d e f]
)
