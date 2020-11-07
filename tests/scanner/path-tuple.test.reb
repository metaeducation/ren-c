;
; Check structure of loaded tuples and paths by analogy.
;
;   * BLOCK! represents PATH!
;   * GROUP! represents TUPLE!
;
; Since you can also put BLOCK! and GROUP! in paths:
;
;   * SYM-BLOCK! represents BLOCK!
;   * SYM-GROUP! represents GROUP!
;
; Each test starts with a string to transcode, then `->`, and then the one or
; more transformed representations of what the string is expected to load as.
;
; Testing in this fashion is important beyond just checking structural
; ambiguities.  Simpler tests like ["/a" -> /a] could just mirror the error,
; e.g. by loading the test incorrectly as ["/a" -> / a] and then validating
; against its own bad load.
;

[(
    tests: [
        "/"  ->  [_ _]
        "//"  ->  [_ _ _]
        "///"  ->  [_ _ _ _]

        "."  ->  (_ _)
        ".."  ->  (_ _ _)
        "..."  ->  (_ _ _ _)

        "/a"  ->  [_ a]
        "//a"  ->  [_ _ a]
        "a/"  ->  [a _]
        "a//"  ->  [a _ _]
        "/a/"  ->  [_ a _]
        "//a//"  ->  [_ _ a _ _]

        "/<tag>/"  ->  [_ <tag> _]

        "(a b)/c"  ->  [@(a b) c]
        "(a b) /c"  ->  @(a b)  [_ c]

        "a.b/c.d"  ->  [(a b) (c d)]
        "a/b.c/d"  ->  [a (b c) d]

        "/a.b/c.d/"  ->  [_ (a b) (c d) _]
        ".a/b.c/d."  ->  [(_ a) (b c) (d _)]

        "./a"  ->  [(_ _) a]
        "/.a"  ->  [_ (_ a)]

        "[a].(b)"  ->  (@[a] @(b))

        "a.. b"  ->  (a _ _)  b
        "a.. /b"  ->  (a _ _)  [_ b]
        "a../b"  ->  [(a _ _) b]

        "/./(a b)/./"  ->  [_ (_ _) @(a b) (_ _) _]

        "a.1.(x)/[a b c]/<d>.2"  ->  [(a 1 @(x)) @[a b c] (<d> 2)]

        "~/projects/"  ->  [~ projects _]
        "~a~.~b~/~c~"  ->  [(~a~ ~b~) ~c~]

        ; === Bad Path Element Tests ===
        ;
        ; TUPLE! can go in PATH! but not vice-versa.  Besides that, only
        ; INTEGER!, WORD!, GROUP!, BLOCK!, TEXT!, TAG!, and VOID! are
        ; currently allowed in either sequence form.

        "/#a"  !!  <scan-invalid>
        "blk/#{}"  !!  <scan-invalid>

        ; === R3-Alpha compatibility hacks ===

        ; GET-WORD! is not legal in Ren-C as a path element due to ambiguities
        ; about `:a/b` being a GET-WORD! in the head position of a PATH! or
        ; a plain WORD! at the head of a GET-PATH!.  Instead, single-element
        ; GROUP!s are made as cheap as GET-WORD! cells in path.

        "a/:b"  ->  [a @(:b)]
        "a/:b/c"  ->  [a @(:b) c]
    ]


    transform: func [
        {Turn PATH!/TUPLE!s into BLOCK!/GROUP!s for validation testing}

        value [any-value!]
        <local> mtype
        <static> mapping (reduce [
            path! block!
            tuple! group!
            block! sym-block!
            group! sym-group!
        ])
    ][
        mtype: select/skip mapping (type of value) 2
        if mtype [
            value: to mtype collect [
                for index 1 (length of value) 1 [
                    keep transform value/(index)
                ]
            ]
        ]
        return value
    ]


    iter: tests
    while [not tail? iter] [
        text: ensure text! iter/1
        iter: my next

        trap [
            items: transcode text
        ] then error -> [
            if iter/1 <> '!! [
                fail ["Unexpected failure on" mold text "->" error/id]
            ]
            iter: my next
            if iter/1 <> to tag! error/id [
                fail ["Error mismatch on" mold text "->" error/id "and not" iter/1]
            ]
            iter: my next
            any [
                tail? iter
                new-line? iter
            ] then [
                continue
            ]
            if error/arg1 <> iter/1 [
                fail ["Error argument mismatch on" mold text "->" error/arg1 "and not" iter/1]
            ]
            iter: my next
            continue
        ]

        assert [iter/1 = '->]
        iter: my next

        compares: copy []

        !!failure!!: does [
            print [mold text "=>" mold items "vs." mold compares]
            fail ["Transformation mismatch for" text]
        ]

        start: true
        for-each v items [
            append/only compares iter/1

            all [
                not start
                any [tail? iter, new-line? iter]
            ] then [
                fail ["Transcode produced unexpected results for:" text]
            ]

            start: false

            t: transform v  ; turns path/tuples to block/group structure

            if t <> iter/1 [
                print ["Expected:" mold iter/1]
                print ["Produced:" mold t]
                !!failure!!
            ]
            iter: my next
        ]

        if not new-line? iter [
            append/only compares iter/1
            !!failure!!
        ]
    ]

    true
)]
