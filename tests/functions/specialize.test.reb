; better-than-nothing SPECIALIZE tests

[
    append-123: specialize :append [value: [1 2 3] only: true]
    [a b c [1 2 3] [1 2 3]] = append-123/dup [a b c] 2
][
    append-123: specialize :append [value: [1 2 3] only: true]
    append-123-twice: specialize :append-123 [dup: true count: 2]
    [a b c [1 2 3] [1 2 3]] = append-123-twice [a b c]
][
    append-10: specialize 'append [value: 10]
    f: make frame! :append-10
    f/series: copy [a b c]
    do f
    [a b c 10 10] = do f
][
    foo: does [
        return-5: specialize 'return [value: 5]
        return-5
        "this shouldn't be returned"
    ]
    foo = 5
]


; doubles as a test for <skip>
[
    test: func [f [<skip> file!] b [block!] i [integer!]] [
        reduce [
            either set? 'f [f] [blank]
            b
            i
        ]
    ]
    
    sp-test: specialize 'test [b: [x y z]]

    all? [
        [_ [x y z] 10] = test [x y z] 10
        [%foo.txt [x y z] 10] = test %foo.txt [x y z] 10
        [_ [x y z] 10] = sp-test 10
        [%foo.txt [x y z] 10] = sp-test %foo.txt 10
    ]
]
