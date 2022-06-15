; Ren-C's QUOTED! is a generic and arbitrary-depth variant of the
; LIT-XXX! types from historical Rebol.
;
; SET and GET should see through escaping and work anyway

(
    unset 'a
    set noquote the '''''a <seta>
    <seta> = get noquote the ''a
)(
    unset 'a
    set noquote the 'a <seta>
    <seta> = get noquote the '''''''a
)(
    a: b: ~
    set noquote first ['''''a] <seta>
    set noquote first [''b] <setb>
    [<seta> <setb>] = collect [
        reduce-each x [noquote 'a noquote '''''''b] [keep get x]
    ]
)

; Test basic binding, e.g. to make sure functions detect SET-WORD!

(
    x: 10
    set 'x: 20
    x = 20
)(
    x: 10
    y: _
    foo: function [] [
        set 'x: 20
        set 'y x
        return
    ]
    foo
    (x = 10) and (y = 20)
)

; Try again, but set a QUOTED! (and not WORD! that results from literal)

(
    x: 10
    set 'x: 20
    x = 20
)(
    x: 10
    y: _
    foo: function [return: <none>] [
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
    y: _
    foo: function [return: <none>] [
        set noquote the '''''''x: 20  ; SET-WORD! gathered, no assignment
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
    (0 = get noquote word) and (1 = get noquote w1) and (2 = get noquote w2)
)(
    foo: function [] [
        a: 0
        o1: make object! [a: 1]
        o2: make object! [a: 2]
        word: ''''''''''a:
        w1: bind word o1
        w2: bind word o2
        return (0 = get noquote word)
            and (1 = get noquote w1)
            and (2 = get noquote w2)
    ]
    foo
)


(null? ')
(null? do ['])
(['] = reduce [''])
([''] = reduce ['''])
([' '' ''' ''''] = reduce ['' ''' '''' '''''])

(
    [1 (2 + 3) [4 + 5] a/+/b c/+/d: :e/+/f]
    = reduce
    ['1 '(2 + 3) '[4 + 5] 'a/+/b 'c/+/d: ':e/+/f]
)

(the '[a b c] = quote [a b c])
(the '(a b c) == quote the (a b c))
(not (the '[A B C] == quote [a b c]))
('''[a b c] !== '''''[a b c])
('''[a b c] == '''[a b c])
('''[a b c] <> '''''[a b c])

; No quote levels is legal for QUOTE to add also, if /DEPTH is 0
[
    (null = quote/depth null 0)
    (<x> = quote/depth <x> 0)
]

; low level "KIND"
[
    (null = kind of null)
    (quoted! = kind of the ')
    (quoted! = kind of the 'foo)
]

((quote word!) = type of the 'foo)  ; higher-level "TYPE"
((type of the ''[a b c]) = quote/depth block! 2)


; REQUOTE is a reframing action that removes quoting levels and then puts
; them back on to the result.

((the ''''3) == requote add the ''''1 2)

((the '''[b c d]) == requote find ''''[a b c d] [b])

(null == requote find ''''[a b c d] [q])  ; nulls exempt

((the '(1 2 3 <four>)) == requote append ''(1 2 3) <four>)

((the '''a/b/c/d/e/f) = requote join the '''a/b/c '/d/e/f)

(
    match-parse3: enclose :parse3 lambda [f] [
        let input: f.input
        do f then [input]
    ]
    (the '[1]) = (requote match-parse3 the '[1] [some integer!])
)

; COPY should be implemented for all types, QUOTED! included.
;
((the '''[a b c]) == copy the '''[a b c])


; All escaped values are truthy, regardless of what it is they are escaping

(did the '_)
(did the '#[false])
(did the ')
(did the ''''''''_)
(did the ''''''''#[false])
(did the '''''''')


; An escaped word that can't fit in a cell and has to do an additional
; allocation will reuse that cell if it can (e.g. on each deliteralization
; step).  However, if that contains an ANY-WORD!, then a binding operation
; on that word will create a new cell allocation...similar to how bindings
; in LIT-WORD! could not be mutated, only create a new LIT-WORD!.
(
    a: 0
    o1: make object! [a: 1]
    o2: make object! [a: 2]
    word: '''''''''a
    w1: bind word o1
    w2: bind word o2
    did all [
        a = 0
        1 = get noquote w1
        2 = get noquote w2
    ]
)

; Smoke test for quoting items of every type

(
    for-each item compose [
        (:+)
        word
        set-word:
        :get-word
        /refinement
        #issue
        'quoted
        pa/th
        set/pa/th
        :get/pa/th
        (the (group))
        [block]
        #{AE1020BD0304EA}
        "text"
        %file
        e@mail
        <tag>
        (make bitset! 16)
        (make image! 10x20)
        (make vector! [integer! 32 100])
        (make map! [m a p !])
        (make varargs! [var args])
        (make object! [obj: {ect}])
        (make frame! :append)
        (make error! "error")
        (port: open http://example.com)
        #[true]
        10
        10.20
        10%
        $10.20
        #"a"
        10x20
        (make typeset! [integer! text!])
        (make gob! [])
        (make event! [type: 'done port: port])
        ("try handle here")
        ("try struct here")
        ("try library here")
        _
        |
        ~void~
    ][
        lit-item: quote get/any 'item

        comment "Just testing for crashes; discards mold result"
        mold :lit-item

        (e1: trap [equal1: equal? get/any 'item get/any 'item]) also [
            e1.where: e1.near: null
        ]
        (e2: trap [equal2: :lit-item = :lit-item]) also [
            e2.where: e2.near: null
        ]
        if e1 [e1.line: null]  ; ignore line difference (file should be same)
        if e2 [e2.line: null]
        if :e1 != :e2 [
            print mold type of get/any 'item
            print mold e1
            print mold e2
            fail "no error parity"
        ]
        if equal1 != equal2 [
            fail "no comparison parity"
        ]
    ]
    close port
    true
)


(
    did all [
        bad-word? x: '~void~
        bad-word? get/any 'x
    ]
)
