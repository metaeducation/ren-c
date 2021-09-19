; %only.test.reb
;
; Creates optimized shallowly immutable single-element block.
; Further optimizations planned:
;
; https://forum.rebol.info/t/1182/13

([] = only null)
([1] = only 1)
([[1]] = only only 1)

; Top-level immutability
(
    block: [x]
    j: only block
    'series-frozen = (trap [append j <illegal>]).id
)

; If contents were mutable, they still will be if contained
(
    block: [x]
    j: only block
    append first j <legal>
    [[x <legal>]] = j
)
