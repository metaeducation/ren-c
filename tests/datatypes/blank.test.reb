; %blank.test.reb

(blank? _)
(blank? blank)
(not blank? 1)
(blank! = type of blank)

(blank = '_)

(null = make blank! maybe null)
(error? trap [make blank! [a b c]])

(null? to blank! maybe null)  ; TO's universal protocol for void 2nd argument
(null? to maybe null 1)  ; TO's universal protocol for blank 1st argument

~bad-cast~ !! (to blank! 1)  ; no other types allow "conversion" to blank

("_" = mold blank)

[#1666 #1650 (
    /f: does [_]
    blank = f
)]

[
    (void? for-each 'x _ [1020])
    ([] = map-each 'x _ [1020])
    (void? for-next 'x _ [1020])
    (all wrap [
        _ = [result count]: remove-each 'x _ [fail "this never gets called"]
        result = _
        count = 0
    ])
    (void? every 'x _ [okay])
    (void? for-skip 'x _ 2 [1020])

    ~nothing-to-take~ !! (take _)
    (null = try take _)
    (null = find _ 304)
    (null = select _ 304)

    ~bad-pick~ !! (pick _ 304)
    (null = try pick _ 304)

    (_ = copy _)  ; do NOT want opt-out of copy
]

; BLANK!s are considered to be EMPTY?, and in accordance with that they report
; that their length is 0.  However they do not have an index.
[
    (empty? _)
    (0 = length of _)
    ~type-has-no-index~ !! (index of _)
    (null = try index of _)
]
