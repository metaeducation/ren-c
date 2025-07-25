; %countify.parse.test.r
;
; This was a strange little function that was written as a demo of how parse
; rules could be generated and used.  So long as it was written off the cuff
; for that demo, it was turned into a test.

[(
    countify: func [things data] [
        let counts: to map! []
        let rules: collect [
            for-each 't things [
                counts.(t): 0
                keep t
                keep compose:deep $(counts.(t): me + 1)
                keep:line '|
            ]
            keep 'veto
        ]
        parse data (compose:deep [
            opt some [(spread rules)]  ; could also be `opt some [rules]`
        ]) except [
            return <outlier>
        ]
        return collect [
            for-each [key value] counts [
                keep key
                keep value
            ]
        ]
    ]
    ok
)(
    ["a" 3 "b" 3 "c" 3] = countify ["a" "b" "c"] "aaabccbbc"
)(
    <outlier> = countify ["a" "b" "c"] "aaabccbbcd"
)]
