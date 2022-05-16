; %parse-ahead.test.reb
;
; AHEAD was called AND in historical Rebol parse, but AHEAD is what
; UPARSE and Red use--it makes more sense.

[
    (error? trap [uparse [] [ahead]])
    ('a == uparse [a] [ahead 'a 'a])
    (1 == uparse [1] [ahead [block! | integer!] <any>])
]

[
    (error? trap [uparse "" [ahead]])
    (#a == uparse "a" [ahead #a #a])
    (#1 == uparse "1" [ahead [#a | #1] <any>])
]

[#1238
    (null = uparse "ab" [ahead "ab" "ac"])
    (null = uparse "ac" [ahead "ab" "ac"])
]
