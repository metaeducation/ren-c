; %onlify.function.test.r
;
; Ren-C eliminated the /ONLY switch from APPEND, INSERT, CHANGE, and FIND.
;
; This demonstrates how it can be put back.  The method is used by Redbol, but
; could be used by anyone who thought they needed it.

[(
onlify: func [
    return: [action!]
    frame [<unrun> frame!]
    :param [word!]
][
    param: default ['value]
    return adapt (
        augment frame [
            :only "Use value literally (don't splice blocks or unquote)"
        ]
    ) compose:deep [
        all [not only, any-list? series, any-list? (param)] then [
            set:any $(param) spread (param)
        ]
        ; ...fall through to normal handling
    ]
]
ok)

(
    append: my onlify
    all [
        [a b c d e] = append [a b c] [d e]
        [a b c [d e]] = append:only [a b c] [d e]
    ]
)

    ; Classic way of subverting splicing is use of APPEND ONLY
    ;
    ([a b c [d e]] = append:only [a b c] [d e])
    ([a b c (d e)] = append:only [a b c] '(d e))
    ([a b c d/e] = append:only [a b c] 'd/e)
    ([a b c [d e]:] = append:only [a b c] '[d e]:)
    ([a b c (d e):] = append:only [a b c] '(d e):)
    ([a b c d.e:] = append:only [a b c] 'd.e:)
    ([a b c :[d e]] = append:only [a b c] ':[d e])
    ([a b c :(d e)] = append:only [a b c] ':(d e))
    ([a b c :d.e] = append:only [a b c] ':d.e)
    ([a b c ^[d e]] = append:only [a b c] '^[d e])
    ([a b c ^(d e)] = append:only [a b c] '^(d e))
    ([a b c ^d/e] = append:only [a b c] '^d/e)

(
    /append-123: specialize append:only/ [value: [1 2 3]]
    /append-123-twice: specialize append-123/ [dup: 2]
    [a b c [1 2 3] [1 2 3]] = append-123-twice copy [a b c]
)

(
    f: make frame! append:only/
    f.series: copy [a b c]
    f.value: [d e f]
    [a b c [d e f]] = eval f
)

(
    /aopd3: specialize append:only/ [
        dup: 3
        part: 1
    ]

    r: [a b c [d e] [d e] [d e]]

    all [
        , r = aopd3 copy [a b c] [d e]
        , r = applique :aopd3 [series: copy [a b c] value: [d e]]
    ]
)

(
    good: 'no

    for-each 'code [
        [specialize append:only:only/ []]
        [specialize append:asdf/ []]
        [
            /apo: specialize append:only/ []
            specialize apo:only/ []
        ]
    ][
        if no? good [
            good: to-yesno (
                'bad-parameter <> (sys.util/recover [eval inside [] code]).id
            )
        ]
    ]

    no? good
)

(
    find: my onlify/param 'pattern
    all [
        [a b c [a b c]] = find [a b c [a b c]] [a b c]
        [[a b c]] = find:only [a b c [a b c]] [a b c]
    ]
)
]
