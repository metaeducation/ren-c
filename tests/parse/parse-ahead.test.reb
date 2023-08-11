; %parse-ahead.test.reb
;
; AHEAD was called AND in historical Rebol parse, but AHEAD is what
; UPARSE and Red use--it makes more sense.

[
    ~???~ !! (parse [] [ahead])
    ('a == parse [a] [ahead 'a 'a])
    (1 == parse [1] [ahead [block! | integer!] <any>])
]

[
    ~???~ !! (parse "" [ahead])
    (#a == parse "a" [ahead #a #a])
    (#1 == parse "1" [ahead [#a | #1] <any>])
]

[#1238
    (raised? parse "ab" [ahead "ab" "ac"])
    (raised? parse "ac" [ahead "ab" "ac"])
]
