; Ren-C's QUOTED! is a generic and arbitrary-depth variant of the
; LIT-XXX! types from historical Rebol.
;
; SET and GET should see through escaping and work anyway

; HEART is how you get the lower-level type out
[
    (quoted! = type of first ['''10])
    (integer! = heart of first ['''10])

    (quasiform! = type of first [~[]~])
    (group! = heart of first [~[]~])

    (quoted! = type of first ['~[]~])
    (group! = heart of first ['~[]~])
]

(
    a: ~
    set noquote the '''''a <seta>
    <seta> = get noquote the ''a
)(
    a: ~
    set noquote the 'a <seta>
    <seta> = get noquote the '''''''a
)(
    a: b: ~
    set inside [] noquote first ['''''a] <seta>
    set inside [] noquote first [''b] <setb>
    [<seta> <setb>] = collect [
        reduce-each x [noquote 'a noquote '''''''b] [keep get inside [] x]
    ]
)

; Test basic binding

(
    x: 10
    set $x: 20
    x = 20
)(
    x: 10
    y: null
    foo: proc [{x}] [
        set $x: 20
        set $y x
    ]
    foo
    (x = 10) and (y = 20)
)

; Try again, but set a QUOTED! (and not WORD! that results from literal)

(
    x: 10
    set $x: 20
    x = 20
)(
    x: 10
    y: null
    foo: proc [{x}] [
        set unquote the 'x: 20
        set unquote the 'y x
    ]
    foo
    (x = 10) and (y = 20)
)

; Now exceed the size of a literal that can be overlaid in a cell

(
    x: 10
    set noquote the ''''''x: 20
    x = 20
)(
    x: 10
    y: null
    foo: proc [
        {x}
    ][
        set noquote the '''''''x: 20  ; x is local assignment
        set noquote the '''''''y x
    ]
    foo
    (x = 10) and (y = 20)
)


; Deeply escaped words try to efficiently share bindings between different
; escapings.  But words in Rebol are historically atomic w.r.t. binding...
; doing a bind on a word returns a new word, vs. changing the binding of
; the word you put in.  Mechanically this means a changed binding must
; detach a deep literal from its existing cell and make new one.
(
    a: 0
    o1: make object! [a: 1]
    o2: make object! [a: 2]
    word: ''''''''''a:
    w1: bind o1 word
    w2: bind o2 word
    all [
        (0 = get noquote inside [] word)
        (1 = get noquote w1)
        (2 = get noquote w2)
    ]
)(
    foo: func [] [
        let a: 0
        let o1: make object! [a: 1]
        let o2: make object! [a: 2]
        let word: ''''''''''a:
        let w1: bind o1 word
        let w2: bind o2 word
        return all [
            (0 = get noquote inside [] word)
            (1 = get noquote w1)
            (2 = get noquote w2)
        ]
    ]
    foo
)


(ghost? ~,~)
(ghost? ^ghost)
(ghost? eval [^ghost])
(ghost? (eval [^ghost]))
(ghost? eval [~,~])
(ghost? eval [, ~,~,])
(ghost? eval [1 + 2, ^ghost])

(
    [1 (2 + 3) [4 + 5] a/+/b c.+.d: :e.+.f]
    = reduce
    ['1 '(2 + 3) '[4 + 5] 'a/+/b 'c.+.d: ':e.+.f]
)

(the '[a b c] = quote [a b c])
(the '(a b c) = quote the (a b c))
(not (the '[A B C] = quote [a b c]))
('''[a b c] != '''''[a b c])
('''[a b c] = '''[a b c])
('''[a b c] <> '''''[a b c])

; No quote levels is legal for QUOTE to add also, if /DEPTH is 0
[
    (<x> = quote:depth <x> 0)
    ~expect-arg~ !! (ghost? quote:depth ^ghost 0)  ; can't quote voids, only lift
]

; low level "KIND"
[
    (quoted! = type of the ')
    (quoted! = type of the 'foo)
]

(quoted! = type of the 'foo)
(quoted! = type of the ''[a b c])


; REQUOTE is a reframing action that removes quoting levels and then puts
; them back on to the result.

((the ''''3) = requote add the ''''1 2)

((the '''[b c d]) = requote find ''''[a b c d] spread [b])

(null = requote find ''''[a b c d] spread [q])  ; nulls exempt

((the '(1 2 3 <four>)) = requote append ''(1 2 3) <four>)

((the '''a/b/c/d/e/f) = requote join the '''a/b/c '/d/e/f)

[
    ((the '[1]) = (requote parse3:match the '[1] [some integer!]))
    (null = (requote parse3:match the '[a] [some integer!]))
]

; COPY should be implemented for all types, QUOTED! included.
;
((the '''[a b c]) = copy the '''[a b c])


; All escaped values are truthy, regardless of what it is they are escaping

(did the ')
(did the '~false~)
(did the '''''''')
(did the ''''''''~false~)


(
    a: 0
    o1: make object! [a: 1]
    o2: make object! [a: 2]
    word: '''''''''a
    w1: bind o1 word
    w2: bind o2 word
    all [
        a = 0
        1 = get noquote w1
        2 = get noquote w2
    ]
)

; Smoke test for quoting items of every type

(
    port: ~
    for-each 'item compose [
        (^+)
        word
        set-word:
        :get-word
        /refinement
        #issue
        'quoted
        pa/th
        tu.ple
        set.tu.ple:
        :get.tu.ple
        (the (group))
        [block]
        #{AE1020BD0304EA}
        "text"
        %file
        e@mail
        <tag>
        (make bitset! 16)

        ; https://github.com/rebol/rebol-issues/issues/2340
        ; (to map! [m a p !])

        (make varargs! [var args])
        (make object! [obj: "ect"])
        (make frame! append/)
        (make warning! "error")
        (port: open http://example.com)
        ~quasiword~
        10
        10.20
        10%
        $10.20
        #"a"
        10x20
        ("try handle here")
        ("try struct here")
        ("try library here")
        _
        |
        ~[_]~
    ] {
        e1: e2: equal1: equal2: ~

        lit-item: quote get meta $item

        comment "Just testing for crashes; discards mold result"
        mold :lit-item

        (e1: rescue [equal1: equal? get meta $item get meta $item]) also [
            e1.where: e1.near: null
        ]
        (e2: rescue [equal2: :lit-item = :lit-item]) also [
            e2.where: e2.near: null
        ]
        if e1 [e1.line: null]  ; ignore line difference (file should be same)
        if e2 [e2.line: null]
        if :e1 != :e2 [
            print mold type of get meta $item
            print mold e1
            print mold e2
            panic "no error parity"
        ]
        if equal1 != equal2 [
            panic "no comparison parity"
        ]
    }
    close port
    ok
)


(
    x: ~
    all [
        quasi? x: '~[]~
        quasi? get meta $x
    ]
)

[
    (do "Rebol [] quit:value ['a] = reduce [''a]")
    (do "Rebol [] quit:value [''a] = reduce ['''a]")
    (do "Rebol [] quit:value ['a ''a ''''a] = reduce [''a '''a '''''a]")
]
