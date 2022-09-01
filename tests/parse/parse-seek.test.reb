; %parse-seek.test.reb
;
; SEEK allows you to seek an arbitrary position in the input series, either
; by integer index or by position.  It tolerates NULL (with TRY) to mean no-op.
;
; Unlike R3-Alpha, changing the series being parsed is not allowed.

("a" == parse "a" [try seek (null) "a"])
("b" == parse "aaabbb" [
    "a" pos: <here> repeat 2 "a" seek (pos) repeat 2 "a" repeat 3 "b"
])
("b" == parse "aaabbb" ["a" repeat 2 "a" seek (2) repeat 2 "a" repeat 3 "b"])

(
    did all [
        "bbcc" == parse "aabbcc" [
            some "a", x: <here>, some "b", y: <here>
            seek (x), z: across to <end>
        ]
        x = "bbcc"
        y = "cc"
        z = "bbcc"
    ]
)

(
    pos: 5
    parse "123456789" [seek (pos) nums: across to <end>]
    nums = "56789"
)

; SEEK INTEGER! (replaces TO/THRU integer!
;
[#1965
    ("cd" == parse "abcd" [seek (3) "cd"])
    ("d" == parse "abcd" [seek (3) <any> "d"])
    (#d == parse "abcd" [seek (4) <any>])
    ("" == parse "abcd" [seek (5)])
    ("abcd" == parse "abcd" ["ab" seek (1) "abcd"])
    ("bcd" == parse "abcd" ["ab" seek (1) <any> "bcd"])
]

; !!! What to do about out-of-range seeks?  It was tolerated historically but
; seems to be a poor practice.  It's an error at the moment.
;
~index-out-of-range~ !! (parse "abcd" [seek (128)])
