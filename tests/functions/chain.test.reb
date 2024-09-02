; better-than-nothing CHAIN tests

(
    add-one: lambda [x] [x + 1]
    mp-ad-ad: chain [get $multiply, get $add-one, get $add-one]
    202 = (mp-ad-ad 10 20)
)
(
    add-one: func [x] [return x + 1]
    mp-ad-ad: chain [get $multiply, get $add-one, get $add-one]
    sub-one: specialize get $subtract [value2: 1]
    mp-normal: chain [get $mp-ad-ad, get $sub-one, get $sub-one]
    200 = (mp-normal 10 20)
)

(
    metaraise: chain [get $raise, get $meta/except]
    e: metaraise ~test~
    all [
        error? e
        e.id = 'test
    ]
)

(
    metatranscode: chain [get $transcode, get $meta/except]
    e: metatranscode "1&e"
    all [
        error? e
        e.id = 'scan-invalid
    ]
)
