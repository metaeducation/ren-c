REBOL [
    File: %make-file.r
    Description: {
        This is a very experimental starter file for defining a dialect that
        can take advantage of generic TUPLE! and PATH! to create FILE!s.

        An obvious benefit to leveraging the structure is basic inspection:

            >> file: 'base/sub/name.ext

            >> first file
            == base  ; word!

            >> third file
            == name.ext  ; tuple!

            >> last third file
            == ext  ; word!

        Where MAKE FILE! comes in is as a dialect with helpful features and
        some prescriptive behavior.  Like stopping file sub-components from
        "reaching out" and adding more structure:

            >> extension: "not/legal"

            >> file: make file! 'some/dir/filename.(extension)
            ** Error: Can't use slash in filename sub-component "not/legal"

        A key thought in the vision for this prescriptiveness is to come up
        with a balanced way of getting the right number of slashes in
        compositions.  The goal is to avoid sloppy guesswork that sometimes
        drops slashes and sometimes adds them in.  This means embracing the
        idea that variables representing directories should end in `/`, and
        making it explicit when they do not.

        There are other potential ideas that could be integrated.  One would
        be using TAG!s to indicate places that should be substituted with
        environment variables.  (This brings up some questions about how to
        deal with Windows variance of having drive-letters and backslashes in
        many of those variables.)

        This will be an ongoing project, as time permits...with hope that it
        evolves into something that can be the main behavior for MAKE FILE!.
    }
]

make-file-block-parts: func [
    return: [block!]
    block [block!]
    <local> last-was-slash
][
    ; Current idea is to analyze for "slash coherence"

    last-was-slash: false

    collect [iterate block [
        item: either group? block/1 [do block/1] [block/1]
        switch type of item [
            refinement! [  ; bootstrap only
                if last-was-slash [
                    fail ["Doubled slash found in MAKE FILE! at" item]
                ]
                keep to text! item
                last-was-slash: false  ; does not end in slash
            ]

            path! [
                case [
                    item = '/ [
                        if last-was-slash [
                            fail ["Doubled slash found in MAKE FILE! at" item]
                        ]
                        keep "/"
                        last-was-slash: true
                    ]

                    all [
                        last-was-slash
                        blank? first item
                    ][
                        fail ["Doubled slash found in MAKE FILE! at" item]
                    ]

                    default [
                        last-was-slash: blank? last item
                        keep to text! item
                    ]
                ]
            ]

            tuple!
            word! [
                keep to text! item
                last-was-slash: false
            ]

            text! [
                if find item "/" [
                    fail "Text components can't contain slashes in MAKE FILE!"
                ]
                keep item
                last-was-slash: false
            ]

            file! [
                all [
                    last-was-slash
                    #"/" = first item
                ] then [
                    fail ["Doubled slash found in MAKE FILE! at" item]
                ]
                keep to text! item
                last-was-slash: #"/" = last item
            ]

            fail 'item
        ]
    ]]
]

make-file-tuple-parts: func [
    return: [block!]
    tuple [tuple!]
    <local> text
][
    tuple: as block! tuple
    collect [iterate tuple [
        item: switch type of tuple/1 [
            group! [do tuple/1]
            block! [fail "Blocks in tuples should reduce or something"]
            default [tuple/1]
        ]

        text: switch type of item [
            text! [item]
            file! [as text! item]
            word! [as text! item]
            integer! [to text! item]
            tag! [  ; use as convenience for environment variables
                (get-env as text! item) else [
                    fail [item "environment variable not set"]
                ]
            ]
        ]

        if find text "/" [
            fail ["Can't have / in file component:" text]
        ]

        keep text

        if not last? tuple [keep #"."]
    ]]
]

make-file-path-parts: func [
    return: [block!]
    path [path!]
][
    path: as block! path
    collect [iterate path [
        item: either group? path/1 [do path/1] [path/1]

        switch type of item [
            word! [
                all [
                    not last? path
                    group? path/1  ; was non-literal, should have been `word/`
                ] then [
                    fail [item "WORD! cannot be used as directory"]
                ]
                keep as text! item
                if not last? path [keep #"/"]
            ]
            text!
            file! [  ; GROUP!-only (couldn't be literally in PATH!)
                all [
                    not tail? path
                    #"/" <> last item
                ] then [
                    fail [item "FILE! must end in / to be used as directory"]
                ]
                keep item
            ]

            tag! [  ; use as convenience for environment variables
                (keep get-env as text! item) else [
                    fail [item "environment variable not set"]
                ]
            ]

            tuple! [  ; not allowed to have slashes in it
                keep make-file-tuple-parts item
            ]

            block! [
                keep make-file-block-parts item
            ]
        ]
    ]]
]

make-file: func [
    {Create a FILE! using the file path specification dialect}

    return: [<opt> file!]
    def [<blank> path! tuple! block!]
][
    ; Note: The bootstrap executable has shaky support for quoting generic
    ; paths, and no support for generic tuples.  It only offers the BLOCK!
    ; dialect form.

    (as file! try unspaced do [
        def: switch type of def [  ; consolidate to BLOCK!-oriented file spec
            path! [make-file-path-parts def]
            tuple! [make-file-tuple-parts def]
            block! [make-file-block-parts def]
        ]
        def
    ]) also file -> [
        if find file "//" [
            fail ["MAKE-FILE of" def "produced double slashes:" file]
        ]
    ] else [
        fail "Empty filename produced in MAKE-FILE"
    ]
]
