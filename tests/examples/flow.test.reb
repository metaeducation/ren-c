; %flow.test.reb
;
; Simple example, if extended could be quite useful

[(
    flow: func [
        block [block!]
        /placeholder [element?]
        <local> flow-result
    ][
        placeholder: default [_]
        block: copy block
        replace/all block placeholder $flow-result
        while [not tail? block] [
            insert block $ 'flow-result:
            [block flow-result]: evaluate/next block
        ]
        return flow-result
    ]
    ok
)
(
   [30 20 10] = flow [
       [1 2 3]
       reverse _
       map-each x _ [x * 10]
   ]
)]
