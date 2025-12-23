; %space.test.r

(space? _)
(space? space)
(not space? 1)
(rune! = type of space)

(space = _)
(space = ')

(null? to opt null 1)  ; TO's universal protocol for space 1st argument

("_" = mold space)

[#1666 #1650 (
    f: does [_]
    space = f
)]

[
    (ghost? for-each 'x hole [1020])
    ([] = map-each 'x hole [1020])
    (ghost? for-next 'x hole [1020])
    (all wrap [
        hole = [result count]: remove-each 'x hole [panic "never gets called"]
        result = hole
        count = 0
    ])
    (ghost? every 'x hole [okay])
    (ghost? for-skip 'x hole 2 [1020])

    ~nothing-to-take~ !! (take [])
    (null = try take ^ghost)
    (null = find ^ghost 304)
    (null = select ^ghost 304)

    (null = pick ^ghost 304)

    (_ = copy _)  ; do NOT want opt-out of copy
]

; (_) is now SPACE, hence no longer considered empty.  Also have no index.
[
    (not empty? _)
    (1 = length of _)
    ~type-has-no-index~ !! (index of _)
    (null = try index of _)
]
