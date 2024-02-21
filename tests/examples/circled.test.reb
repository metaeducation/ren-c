; %circled.test.reb
;
; https://forum.rebol.info/t/the-circled-dialect-example-of-the-form/1849

[
    (circled: lambda [block [block!] <local> result] [
        parse block [
            result: try thru subparse group! [
                <any> <end> | (fail "Circled Items Must Be Singular")
            ]
            try [thru group! (fail "Only One Circle")]
            accept (result)
        ]
    ], true)

    ('b = circled [a (b) c])
    (null = circled [a b c])

    ~???~ !! (circled [a (b c)])  ; circle singular item
    ~???~ !! (circled [(a) b (c)])  ; only one circle
]
