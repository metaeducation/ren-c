; better-than-nothing CHAIN tests

(
    add-one: lambda [x] [x + 1]
    mp-ad-ad: chain [:multiply, :add-one, :add-one]
    202 = (mp-ad-ad 10 20)
)
(
    add-one: func [x] [return x + 1]
    mp-ad-ad: chain [:multiply, :add-one, :add-one]
    sub-one: specialize :subtract [value2: 1]
    mp-normal: chain [:mp-ad-ad, :sub-one, :sub-one]
    200 = (mp-normal 10 20)
)

(
    metaraise: chain [:raise get $meta/except]
    e: metaraise ~test~
    all [
        error? e
        e.id = 'test
    ]
)

(
    metatranscode: chain [:transcode get $meta/except]
    e: metatranscode "1&e"
    all [
        error? e
        e.id = 'scan-invalid
    ]
)
