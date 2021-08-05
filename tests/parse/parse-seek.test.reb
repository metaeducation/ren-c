; %parse-seek.test.reb
;
; SEEK allows you to seek an arbitrary position in the input series, either
; by integer index or by position.  It tolerates BLANK! to mean no-op.
;
; Unlike R3-Alpha, changing the series being parsed is not allowed.

(uparse? "a" [seek (_) "a"])
(uparse? "aaabbb" ["a" pos: <here> 2 "a" seek (pos) 2 "a" 3 "b"])
(uparse? "aaabbb" ["a" 2 "a" seek (2) 2 "a" 3 "b"])

(
    did all [
        uparse? "aabbcc" [
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
    (uparse? "abcd" [seek (3) "cd"])
    (uparse? "abcd" [seek (3) skip "d"])
    (uparse? "abcd" [seek (4) skip])
    (uparse? "abcd" [seek (5)])
    (uparse? "abcd" ["ab" seek (1) "abcd"])
    (uparse? "abcd" ["ab" seek (1) skip "bcd"])
]

; !!! What to do about out-of-range skips?  It was tolerated historically but
; seems to be a poor practice:
;
;    (uparse? "abcd" [seek (128)])
