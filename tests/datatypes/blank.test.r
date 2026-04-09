; %space.test.r

(space? _)
(space? space)
(not space? 1)
(rune! = type of space)

(space = _)
(space = '_)

(null? to cond null 1)

("_" = mold space)

[#1666 #1650 (
    f: does [_]
    space = f
)]

[
    (void? for-each 'x none [1020])
    ([] = map-each 'x none [1020])
    (void? for-next 'x none [1020])
    (all {
        none = [result count]: remove-each 'x none [panic "never gets called"]
        result = none
        count = 0
    })
    (void? every 'x none [okay])
    (void? for-skip 'x none 2 [1020])

    ~nothing-to-take~ !! (take [])

    ~???~ !! (try take ^void)
    ~???~ !! (find ^void 304)
    ~???~ !! (select ^void 304)
    ~???~ !! (pick ^void 304)

    (null = try take veto)
    (null = find veto 304)
    (null = select veto 304)
    (null = pick veto 304)

    (_ = copy _)  ; do NOT want opt-out of copy
]

; (_) is now SPACE, hence no longer considered empty.  Also have no index.
[
    (not empty? _)
    (1 = length of _)
    ~type-has-no-index~ !! (index of _)
    (null = try index of _)
]
