; %envelop.test.reb
;
; Generic way of surrounding things in lists.

(
    all wrap [
        a: 1020
        [(a)] = block: envelop $[()] 'a  ; uses binding of block
        1020 = eval block
    ]
)
(
    all wrap [
        a: 1020
        [(a)] = block: envelop '[()] 'a  ; no binding passed in, so unbound
        e: sys.util/rescue [eval block]
        e.id = 'not-bound
    ]
)

('{{{a b c}}} = envelop '{{{}}} spread [a b c])

([* [a] *] = envelop '[* *] [a])
