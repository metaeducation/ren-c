; functions/series/pick.r

<64bit> ~out-of-range~ !! (pick at [1 2 3 4 5] 3 -9223372036854775808)

(
    data: [1 2 3 4 5]
    for-each [result pos index] [
        ~bad-pick~ 3 -2147483648
        ~bad-pick~ 3 -2147483647
        ~bad-pick~ 3 -3
        1 3 -2
        2 3 -1
        ~bad-pick~ 3 0
        3 3 1
        4 3 2
        5 3 3
        ~bad-pick~ 3 4
        ~bad-pick~ 3 2147483647
    ][
        assert compose:deep [
            result = eval [pick at data (pos) (index)] except e -> [quasi e.id]
        ]
    ]
    ok
)

<64bit> ~out-of-range~ !! (pick at [1 2 3 4 5] 3 9223372036854775807)

; string

<64bit> ~out-of-range~ !! (pick at "12345" 3 -9223372036854775808)

(
    data: "12345"
    for-each [result pos index] [
        ~bad-pick~ 3 -2147483648
        ~bad-pick~ 3 -2147483647
        ~bad-pick~ 3 -3
        #1 3 -2
        #2 3 -1
        ~bad-pick~ 3 0  ; #857
        #3 3 1
        #4 3 2
        #5 3 3
        ~bad-pick~ 3 4
        ~bad-pick~ 3 2147483647
    ][
        assert compose:deep [
            result = eval [pick at data (pos) (index)] except e -> [quasi e.id]
        ]
    ]
    ok
)


<64bit> ~out-of-range~ !! (pick at "12345" 3 9223372036854775807)

[#2312 (
    data: [<a> <b>]
    all [
        null = try data.(0.5)
        null = try data.(0.999999)
        <a> = data.(1.0)
        <a> = data.(1.5)
        <b> = data.(2.0)
        <b> = data.(2.000000001)
        null = try data.(3.0)
    ]
)]

[#2312 (
    data: "ab"
    all [
        null = try data.(0.5)
        null = try data.(0.999999)
        #"a" = data.(1.0)
        #"a" = data.(1.5)
        #"b" = data.(2.0)
        #"b" = data.(2.000000001)
        null = try data.(3.0)
    ]
)]
