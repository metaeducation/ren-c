REBOL [
    File: %make-file.r
    Title: "MAKE-FILE Experiments"
    Type: module
    Name: Make-File
    Exports: [make-file]
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

doubled-file-slash-error: lambda [item] [
    make error! [
        id: 'doubled-file-slash
        message: ["Doubled / encountered while generating filename:" :arg1]
        arg1: item
    ]
]

embedded-file-slash-error: lambda [item] [
    make error! [
        id: 'embedded-file-slash
        message: ["Embedded `/` encountered in filename component:" :arg1]
        arg1: item
    ]
]

make-file-block-parts: func [
    return: [block!]
    block [block!]
    predicate [action?]
    <local> last-was-slash item
][
    ; Current idea is to analyze for "slash coherence"

    last-was-slash: 'no

    return collect [iterate @block [
        item: either group? block.1 [eval inside block block.1] [block.1]

        item: predicate item

        switch:type item [
            void?! []

            path! [
                case [
                    item = '/ [
                        if yes? last-was-slash [
                            fail doubled-file-slash-error item
                        ]
                        keep "/"
                        last-was-slash: 'yes
                    ]

                    all [
                        yes? last-was-slash
                        blank? first item
                    ][
                        fail doubled-file-slash-error item
                    ]
                ] else [
                    last-was-slash: to-yesno blank? last item
                    keep to text! item
                ]
            ]

            tuple!
            word! [
                keep to text! item
                last-was-slash: 'no
            ]

            text! [
                if find item "/" [
                    fail embedded-file-slash-error item
                ]
                keep item
                last-was-slash: 'no
            ]

            integer! [
                keep item
            ]

            file! [
                all [
                    yes? last-was-slash
                    #"/" = first item
                ] then [
                    fail doubled-file-slash-error item
                ]
                keep to text! item
                last-was-slash: to-yesno #"/" = last item
            ]

            fail ["Bad MAKE-FILE item:" item]
        ]
    ]]
]

make-file-tuple-parts: func [
    return: [block!]
    tuple [tuple!]
    predicate [action?]
    <local> text item
][
    tuple: as block! tuple
    return collect [iterate @tuple [
        item: switch:type tuple.1 [
            group! [eval inside tuple tuple.1]
            block! [fail "Blocks in tuples should reduce or something"]
        ] else [
            tuple.1
        ]

        item: predicate item

        text: switch:type item [
            blank! [null]
            text! [item]
            file! [as text! item]
            word! [as text! item]
            integer! [to text! item]
            fail ["Unknown tuple component type"]
        ]

        if find maybe text "/" [
            fail embedded-file-slash-error text
        ]

        keep maybe text

        if not last? tuple [keep #"."]
    ]]
]

make-file-path-parts: func [
    return: [block!]
    path [path!]
    predicate [action?]
    <local> item
][
    path: as block! path
    return collect [iterate @path [
        item: either group? path.1 [eval inside path path.1] [path.1]

        item: predicate item

        switch:type item [
            blank! []  ; just there as placeholder to get the "/"

            word! [
                all [
                    not last? path
                    group? path.1  ; was non-literal, should have been `word/`
                ] then [
                    fail [item "WORD! cannot be used as directory"]
                ]
                keep as text! item
            ]
            text!
            file! [  ; GROUP!-only (couldn't be literally in PATH!)
                all [
                    not last? path
                    #"/" <> last item
                ] then [
                    fail [item "FILE! must end in / to be used as directory"]
                ]
                keep item
                continue  ; don't add slash, already accounted for
            ]

            tuple! [  ; not allowed to have slashes in it
                keep spread make-file-tuple-parts item :predicate
            ]

            integer! [
                keep item
            ]

            block! [
                keep spread make-file-block-parts item :predicate
            ]
        ]

        if not last? path [keep #"/"]
    ]]
]

make-file: func [
    "Create a FILE! using the file path specification dialect"

    return: [~null~ file!]
    def [<maybe> word! path! tuple! block!]
    :predicate [action?]
    <local> result
][
    predicate: default [:identity]

    ; Note: The bootstrap executable has shaky support for quoting generic
    ; paths, and no support for generic tuples.  It only offers the BLOCK!
    ; dialect form.

    result: as file! unspaced switch:type def [
        ; consolidate to BLOCK!-oriented file spec
        word! [to text! predicate def]
        path! [make-file-path-parts def :predicate]
        tuple! [make-file-tuple-parts def :predicate]
        block! [make-file-block-parts def :predicate]
    ] else [
        fail "Empty filename produced in MAKE-FILE"
    ]

    if find result "//" [
        fail doubled-file-slash-error result
    ]

    ; The following adjustment was in code called "fix-win32-path" that was
    ; in the bootstrap code.  It is either important and should be part of
    ; mainstream file generation, or unnecessary.  Folding it in here for now.
    ;
    all [
        3 = fourth system.version  ; Windows system
        #":" = try second result
        drive: try first path
        any [
            (#"A" <= drive) and (#"Z" >= drive)
            (#"a" <= drive) and (#"z" >= drive)
        ]
    ] then [
        insert result #"/"
        remove skip result 2  ; remove ":"
    ]

    return result
]
