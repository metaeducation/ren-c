; Ren-C's QUOTED! is a generic and arbitrary-depth variant of the
; LIT-XXX! types from historical Rebol.
;
; SET and GET should see through escaping and work anyway

; HEART is how you get the lower-level type out
(
    (quoted! = type of first ['''10])
    (integer! = heart of first ['''10])

    (quasiform! = type of first [~()~])
    (group! = heart of first [~()~])

    (quoted! = type of first ['~()~])
    (group! = heart of first ['~()~])
)

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
    /foo: func [<local> x] [
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
    /foo: func [return: [~] <local> x] [
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
    /foo: func [
        return: [~]
        <local> x
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
    w1: bind word o1
    w2: bind word o2
    all [
        (0 = get noquote inside [] word)
        (1 = get noquote w1)
        (2 = get noquote w2)
    ]
)(
    /foo: func [] [
        let a: 0
        let o1: make object! [a: 1]
        let o2: make object! [a: 2]
        let word: ''''''''''a:
        let w1: bind word o1
        let w2: bind word o2
        return all [
            (0 = get noquote inside [] word)
            (1 = get noquote w1)
            (2 = get noquote w2)
        ]
    ]
    foo
)


(void? ~void~)
(void? eval [~void~])

(
    [1 (2 + 3) [4 + 5] a/+/b c.+.d: :e.+.f]
    = reduce
    ['1 '(2 + 3) '[4 + 5] 'a/+/b 'c.+.d: ':e.+.f]
)

(the '[a b c] = quote [a b c])
(the '(a b c) == quote the (a b c))
(not (the '[A B C] == quote [a b c]))
('''[a b c] !== '''''[a b c])
('''[a b c] == '''[a b c])
('''[a b c] <> '''''[a b c])

; No quote levels is legal for QUOTE to add also, if /DEPTH is 0
[
    (<x> = quote:depth <x> 0)
    ~expect-arg~ !! (void? quote:depth void 0)  ; can't quote voids, only meta
]

; low level "KIND"
[
    (quoted! = type of the '_)
    (quoted! = type of the 'foo)
]

(quoted! = type of the 'foo)
(quoted! = type of the ''[a b c])


; REQUOTE is a reframing action that removes quoting levels and then puts
; them back on to the result.

((the ''''3) == requote add the ''''1 2)

((the '''[b c d]) == requote find ''''[a b c d] spread [b])

(null == requote find ''''[a b c d] spread [q])  ; nulls exempt

((the '(1 2 3 <four>)) == requote append ''(1 2 3) <four>)

((the '''a/b/c/d/e/f) = requote join the '''a/b/c '/d/e/f)

[
    ((the '[1]) = (requote parse3:match the '[1] [some integer!]))
    (null = (requote parse3:match the '[a] [some integer!]))
]

; COPY should be implemented for all types, QUOTED! included.
;
((the '''[a b c]) == copy the '''[a b c])


; All escaped values are truthy, regardless of what it is they are escaping

(did the '_)
(did the '~false~)
(did the ''''''''_)
(did the ''''''''~false~)


; An escaped word that can't fit in a cell and has to do an additional
; allocation will reuse that cell if it can (e.g. on each deliteralization
; step).  However, if that contains an ANY-WORD?, then a binding operation
; on that word will create a new cell allocation...similar to how bindings
; in LIT-WORD! could not be mutated, only create a new LIT-WORD!.
(
    a: 0
    o1: make object! [a: 1]
    o2: make object! [a: 2]
    word: '''''''''a
    w1: bind word o1
    w2: bind word o2
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
        ; (make map! [m a p !])

        (make varargs! [var args])
        (make object! [obj: "ect"])
        (make frame! unrun :append)
        (make error! "error")
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
        ~(_)~
    ] wrap [
        e1: e2: equal1: equal2: ~

        lit-item: quote get:any $item

        comment "Just testing for crashes; discards mold result"
        mold :lit-item

        (e1: trap [equal1: equal? get:any $item get:any $item]) also [
            e1.where: e1.near: null
        ]
        (e2: trap [equal2: :lit-item = :lit-item]) also [
            e2.where: e2.near: null
        ]
        if e1 [e1.line: null]  ; ignore line difference (file should be same)
        if e2 [e2.line: null]
        if :e1 != :e2 [
            print mold type of get:any $item
            print mold e1
            print mold e2
            fail "no error parity"
        ]
        if equal1 != equal2 [
            fail "no comparison parity"
        ]
    ]
    close port
    ok
)


(
    x: ~
    all [
        quasi? x: '~()~
        quasi? get:any $x
    ]
)

[
    (do "Rebol [] quit:value ['] = reduce ['']")
    (do "Rebol [] quit:value [''] = reduce [''']")
    (do "Rebol [] quit:value [' '' ''' ''''] = reduce ['' ''' '''' ''''']")
]
