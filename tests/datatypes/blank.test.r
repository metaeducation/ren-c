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
    (void? for-each 'x hole [1020])
    ([] = map-each 'x hole [1020])
    (void? for-next 'x hole [1020])
    (all wrap [
        hole = [result count]: remove-each 'x hole [panic "never gets called"]
        result = hole
        count = 0
    ])
    (void? every 'x hole [okay])
    (void? for-skip 'x hole 2 [1020])

    ~nothing-to-take~ !! (take [])
    (null = try take ^void)
    (null = find ^void 304)
    (null = select ^void 304)

    (null = pick ^void 304)

    (_ = copy _)  ; do NOT want opt-out of copy
]

; (_) is now SPACE, hence no longer considered empty.  Also have no index.
[
    (not empty? _)
    (1 = length of _)
    ~type-has-no-index~ !! (index of _)
    (null = try index of _)
]
