; %blank.test.reb

(blank? _)
(blank? blank)
(not blank? 1)
(blank! = kind of blank)

(blank = '_)

(null = make blank! maybe null)
(error? trap [make blank! [a b c]])

(null? to blank! maybe null)  ; TO's universal protocol for void 2nd argument
(null? to maybe null 1)  ; TO's universal protocol for blank 1st argument

~???~ !! (to blank! 1)  ; no other types allow "conversion" to blank

("_" = mold blank)

[#1666 #1650 (
    f: does [_]
    blank = f
)]

[
    (void? for-each x _ [1020])
    ([] = map-each x _ [1020])
    (void? for-next x _ [1020])
    (0 = remove-each x _ [true])
    (void? every x _ [true])
    (void? for-skip x _ 2 [1020])

    ~nothing-to-take~ !! (take _)
    (null = try take _)
    (null = find _ 304)
    (null = select _ 304)
    (null = pick _ 304)

    (_ = copy _)  ; do NOT want opt-out of copy
]
