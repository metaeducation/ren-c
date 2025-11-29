; %parse-seek.test.r
;
; SEEK allows you to seek an arbitrary position in the input series, either
; by integer index or by position.  It tolerates NULL (with TRY) to mean no-op.
;
; Unlike R3-Alpha, changing the series being parsed is not allowed.

~???~ !! (parse "a" [seek (null) "a"])  ; null seeks are errors

("a" = parse "a" [seek (^void) "a"])  ; void seeks are no-ops

(
    pos: ~
    "b" = parse "aaabbb" [
        "a" pos: <here> repeat 2 "a" seek (pos) repeat 2 "a" repeat 3 "b"
    ]
)
("b" = parse "aaabbb" ["a" repeat 2 "a" seek (2) repeat 2 "a" repeat 3 "b"])

(
    all [
        let [x y z]
        "bbcc" = parse "aabbcc" [
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
    nums: ~
    parse "123456789" [seek (pos) nums: across to <end>]
    nums = "56789"
)

; SEEK INTEGER! (replaces TO/THRU integer!
;
[#1965
    ("cd" = parse "abcd" [seek (3) "cd"])
    ("d" = parse "abcd" [seek (3) next "d"])
    (#d = parse "abcd" [seek (4) one])
    ("" = parse "abcd" [seek (5)])
    ("abcd" = parse "abcd" ["ab" seek (1) "abcd"])
    ("bcd" = parse "abcd" ["ab" seek (1) next "bcd"])
]

; !!! What to do about out-of-range seeks?  It was tolerated historically but
; seems to be a poor practice.
;
~parse-incomplete~ !! (parse "abcd" [seek (128)])
