REBOL [
    Title: "ODBC Open Database Connectivity Scheme"

    Name: ODBC
    Type: Module

    Version: 0.6.0

    Rights: [
        "Copyright (C) 2010-2011 Christian Ensel" (MIT License)
        "Copyright (C) 2017-2019 Ren-C Open Source Contributors" (Apache)
    ]

    License: {Apache 2.0}
]

; These are the native coded support routines that are needed to be built from
; C in order to interface with ODBC.  The scheme code is the usermode support
; to provide a higher level interface.
;
; open-connection: native [spec [text!]]
; open-statement: native [connection [object!] statement [object!]]
; insert-odbc: native [statement [object!] sql [block!]]
; copy-odbc: native [statement [object!] length [integer!]]
; close-statement: native [statement [object!]]
; close-connection: native [connection [object!]]
; update-odbc: native [connection [object!] access [logic!] commit [logic!]]


database-prototype: context [
    hdbc: '  ; SQLHDBC handle!
    statements: []  ; statement objects
]

statement-prototype: context [
    database: ~
    hstmt: '  ; SQLHSTMT
    string: _
    titles: ~
    columns: '
]

export odbc-statement-of: func [
    {Get a statement port from a connection port}
    return: [port!]
    port [port!]
][
    ; !!! This used to be FIRST but that mixes up matters with pathing
    ; and picking in ways that has to be sorted out.  So it's now a
    ; separate function.

    statement: make statement-prototype []

    database: statement.database: port.locals

    open-statement database statement

    port: lib.open port.spec.ref
    port.locals: statement

    append database.statements port

    return port
]

sys.make-scheme [
    name:  'odbc
    title: "ODBC Open Database Connectivity Scheme"

    actor: context [
        open: function [
            {Open a database port}
            port [port!]
                {WORD! spec then assume DSN, else BLOCK! DSN-less datasource}
        ][
            port.state: context [
                access: 'write
                commit: 'auto
            ]

            port.locals: open-connection case [
                text? spec: select port.spec 'target [spec]
                text? spec: select port.spec 'host [unspaced ["dsn=" spec]]

                cause-error 'access 'invalid-spec port.spec
            ]

            port
        ]

        pick: function [
            port [port!]
            index
                {Index to pick from (only supports 1, for FIRST)}
        ][
            ; !!! This is now ODBC-STATEMENT-OF
        ]

        update: function [port [port!]] [
            if get in connection: port.locals 'hdbc [
                (update-odbc
                    connection
                    port.state.access = 'write
                    port.state.commit = 'auto
                )
                return port
            ]
        ]

        close: function [
            {Closes a statement port only or a database port w/all statements}
            return: <none>
            port [port!]
        ][
            if get try in (statement: port.locals) 'hstmt [
                remove find head statement.database.statements port
                close-statement statement
                return
            ]

            if get try in (connection: port.locals) 'hdbc [
                for-each stmt-port connection.statements [close stmt-port]
                clear connection.statements
                close-connection connection
                return
            ]
        ]

        insert: function [
            port [port!]
            sql [text! word! block!]
                {SQL statement or catalog, parameter blocks are reduced first}
        ][
            insert-odbc port.locals reduce compose [((sql))]
        ]

        copy: function [port [port!] /part [integer!]] [
            copy-odbc/part port.locals part
        ]
    ]
]


sqlform: func [
    {Helper for producing SQL string from SQL dialect}

    return: "Formed SQL string with ? in parameter spots"
        [text! issue!]  ; issue will not have spaces around it when rendered

    parameters "Parameter block being gathered"
        [block!]
    value "Item to form"
        [any-value!]
][
    switch type of :value [
        comma! [join #"," space]  ; avoid spacing before the comma

        integer! word! [as text! value]
        tuple! [mold value]

        ; !!! We need to express literal SQL somehow.  Things like 'T' might be
        ; legal SQL but it's a bad pun to have that be a QUOTED! WORD! named
        ; {T'}.  The only answers we have are to either use full on string
        ; interpolation and escape Rebol variables into it (not yet available)
        ; or to make it so a string in SQL is double-stringed, e.g. {"foo"}.
        ;
        ; We *could* make it so that this is done via some special operator,
        ; like ^({'T'}) to say "hey I want this to be spliced in the statement.
        ; Then "foo" and {foo} could be treated as SQL strings.  Review.
        ;
        text! [value]

        group! [  ; recurse so that nested @var get added to parameters
            spaced collect [
                keep "("
                for-next pos value [
                    if new-line? pos [keep newline]
                    keep sqlform parameters :pos.1
                ]
                if new-line? tail value [keep newline]
                keep ")"
            ]
        ]

        the-word! the-tuple! the-path! [  ; !!! Should THE-PATH be allowed?
            append/only parameters [^ #]: get value
            "?"
        ]

        the-group! [
            append/only parameters ^ do as block! value
            "?"
        ]

        meta-word! meta-tuple! meta-path! [
            as text! [# #]: get value
        ]

        meta-group! [
            as text! do as block! value
        ]
    ]
    else [
        fail ["Invalid type for ODBC-EXECUTE dialect:" mold/limit :value 60]
    ]
]


; https://forum.rebol.info/t/1234
;
odbc-execute: func [
    {Run a query in the ODBC extension using the Rebol SQL query dialect}

    statement [port!]
    query "SQL text, or block that runs SPACED with ^^(...) as parameters"
        [text! block!]
    /parameters "Explicit parameters (used if SQL string contains `?`)"
        [block!]
    /verbose "Show the SQL string before running it"
][
    parameters: default [copy []]

    if block? query [
        query: spaced collect [
            let is-first: true
            for-next pos query [
                if not is-first [
                    if new-line? pos [keep newline]
                ]
                is-first: false
                keep sqlform parameters :pos.1
            ]
        ]
    ]

    if verbose [
        print ["** SQL:" mold query]
        print ["** PARAMETERS:" mold parameters]
    ]

    insert statement compose [(query) ((parameters))]
]

export [odbc-execute]
