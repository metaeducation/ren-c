; %parse-seek.test.reb
;
; SEEK allows you to seek an arbitrary position in the input series, either
; by integer index or by position.  It tolerates BLANK! to mean no-op.
;
; Unlike R3-Alpha, changing the series being parsed is not allowed.

("a" == uparse "a" [seek (_) "a"])
("b" == uparse "aaabbb" ["a" pos: <here> 2 "a" seek (pos) 2 "a" 3 "b"])
("b" == uparse "aaabbb" ["a" 2 "a" seek (2) 2 "a" 3 "b"])

(
    did all [
        "bbcc" == uparse "aabbcc" [
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
    uparse "123456789" [seek (pos) nums: across to <end>]
    nums = "56789"
)

; SEEK INTEGER! (replaces TO/THRU integer!
;
[#1965
    ("cd" == uparse "abcd" [seek (3) "cd"])
    ("d" == uparse "abcd" [seek (3) <any> "d"])
    ("d" == uparse "abcd" [seek (4) <any>])
    ("" == uparse "abcd" [seek (5)])
    ("abcd" == uparse "abcd" ["ab" seek (1) "abcd"])
    ("bcd" == uparse "abcd" ["ab" seek (1) <any> "bcd"])
]

; !!! What to do about out-of-range seeks?  It was tolerated historically but
; seems to be a poor practice.  It's an error at the moment.
;
(error? trap [uparse "abcd" [seek (128)]])
