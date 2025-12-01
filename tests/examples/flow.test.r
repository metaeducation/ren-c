; %flow.test.r
;
; Simple example, if extended could be quite useful

[(
    flow: func [
        block [block!]
        :placeholder [element?]
        {flow-result}
    ][
        placeholder: default [_]
        block: copy block
        replace block placeholder $flow-result
        until [tail? block] [
            insert block $flow-result:
            [block flow-result]: evaluate:step block
        ]
        return flow-result
    ]
    ok
)
(
   [30 20 10] = flow [
       [1 2 3]
       reverse _
       map-each 'x _ [x * 10]
   ]
)]
