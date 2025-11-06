Rebol [
    title: "ODBC Open Database Connectivity Scheme"

    name: ODBC
    type: module

    version: 0.6.0

    rights: [
        "Copyright (C) 2010-2011 Christian Ensel" (MIT License)
        "Copyright (C) 2017-2019 Ren-C Open Source Contributors" (Apache)
    ]

    license: "Apache 2.0"
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
; update-odbc: native [connection [object!] access [logic?] commit [logic?]]


database-prototype: context [
    hdbc: null  ; SQLHDBC handle!
    statements: []  ; statement objects
]

statement-prototype: context [
    database: ~
    hstmt: null  ; SQLHSTMT
    string: null
    titles: ~
    columns: null
]

export odbc-statement-of: func [
    "Get a statement port from a connection port"
    return: [port!]
    port [port!]
][
    ; !!! This used to be FIRST but that mixes up matters with pathing
    ; and picking in ways that has to be sorted out.  So it's now a
    ; separate function.

    let statement: make statement-prototype []

    let database: statement.database: port.locals

    open-statement database statement

    port: lib/open port.spec.ref
    port.locals: statement

    append database.statements port

    return port
]

sys.util/make-scheme [
    name:  'odbc
    title: "ODBC Open Database Connectivity Scheme"

    actor: context [
        open: func [
            "Open a database port"
            port "WORD! spec then assume DSN, else BLOCK! DSN-less datasource"
                [port!]
        ][
            port.state: context [
                access: 'write
                commit: 'auto
            ]

            let spec
            port.locals: open-connection case [
                text? opt spec: select port.spec 'target [spec]
                text? opt spec: select port.spec 'host [unspaced ["dsn=" spec]]

                cause-error 'access 'invalid-spec port.spec
            ]

            return port
        ]

        pick: func [
            port [port!]
            index "Index to pick from (only supports 1, for FIRST)"
        ][
            panic "This is now ODBC-STATEMENT-OF"
        ]

        update: func [return: [port!] port [port!]] [
            if get in connection: port.locals 'hdbc [
                (update-odbc
                    connection
                    port.state.access = 'write
                    port.state.commit = 'auto
                )
                return port
            ]
        ]

        close: func [
            "Closes a statement port only or a database port w/all statements"
            return: [port!]
            port [port!]
        ][
            let statement: port.locals
            if get opt has statement 'hstmt [
                remove find (head of statement.database.statements) port
                close-statement statement
                return port
            ]

            let connection: port.locals
            if get opt has connection 'hdbc [
                for-each 'stmt-port connection.statements [close stmt-port]
                clear connection.statements
                close-connection connection
                return port
            ]

            return port  ; !!! should this warning?
        ]

        insert: func [
            return: [integer! block!]
            port [port!]
            sql "SQL statement or catalog, parameter blocks reduced first"
                [text! word! block!]
        ][
            return insert-odbc port.locals blockify sql
        ]

        copy: func [port [port!] :part [integer!]] [
            return copy-odbc:part port.locals part
        ]
    ]
]


sqlform: func [
    "Helper for producing SQL string from SQL dialect"

    return: "Formed SQL string with ? in parameter spots, $GROUP! may vaporize"
        [null? text! rune!]  ; rune won't have spaces around it when rendered

    parameters "Parameter block being gathered"
        [block!]
    value "Item to form"
        [element?]
][
    return switch:type value [
        block! [  ; recursive behavior (kicks off the process, $[] reuses...)
            spaced collect [
                let is-first: okay
                for-next 'pos value [
                    if not is-first [
                        if new-line? pos [keep newline]
                    ]
                    is-first: null
                    keep opt sqlform parameters inside value pos.1
                ]
            ]
        ]

        comma! [join #"," space]  ; avoid spacing before the comma

        integer! [form value]
        word! [as text! value]

        ; !!! Rather than use string interpolation, we turn WORD!s into SQL
        ; text...but that can't cover everything.  Using TEXT! for that
        ; literal SQL has the problem that it looks like it has quotes around
        ; it, however the quotes don't appear in the SQL.  So you have to
        ; double escape to get quotes:
        ;
        ;     -["this will be quoted in the generated SQL"]-
        ;     "this is literal SQL, with no quotes"
        ;
        ; Better ideas welcome.  But that's just the dialect state for now.
        ;
        text! [value]

        group! [  ; recurse so that nested $var get added to parameters
            spaced collect [
                keep "("
                for-next 'pos value [
                    if new-line? pos [keep newline]
                    keep opt sqlform parameters inside value pos.1
                ]
                if new-line? tail of value [keep newline]
                keep ")"
            ]
        ]

        word?:tied/ [  ; $var
            append parameters get untie value
            "?"
        ]

        group?:tied/ [  ; $(expr)
            append parameters eval untie value
            "?"
        ]

        block?:tied/ [  ; $[expr] ... splice *non* text (where ? won't work)
            value: untie value
            let safe: okay
            if <!> = try first value [
                value: next value
                safe: null
            ]
            let ^product: eval value
            if void? ^product [
                return null
            ]
            if safe and (text? product) [
                panic [
                    "$[expr] disallows TEXT! to protect against SQL injection"
                    "You can use LOAD-ed data: BLOCK!s will be formed as SQL"
                    "and things like WORD! and INTEGER! work.  If you are able"
                    "to, you should favor the $var or $(expr) tools which use"
                    "parameterized query and splits out the data, leaving a ?"
                    "in the spot for the driver...avoiding confusing data for"
                    "code, and that will support strings.  But only limited"
                    "spots accept this.  If you're SURE you know what you are"
                    "doing and want to splice a string, use $[<!> expr] to"
                    "indicate you are aware of the risks."
                ]
            ]
            switch type of product [
                text! [product]
                word! [as text! product]
                integer! [form product]
                block! [sqlform parameters product]
                group! [sqlform parameters product]
            ] else [
                panic ["Bad type in $[expr] usage"]
            ]
        ]
    ]
    else [
        panic ["Invalid type for ODBC-EXECUTE dialect:" mold:limit value 60]
    ]
]


; https://forum.rebol.info/t/1234
;
odbc-execute: func [
    "Run a query in the ODBC extension using the Rebol SQL query dialect"

    statement [port!]
    query "SQL text, or block that runs SPACED with $(...) as parameters"
        [text! block!]
    :parameters "Explicit parameters (used if SQL string contains `?`)"
        [block!]
    :verbose "Show the SQL string before running it"
][
    parameters: default [copy []]

    if block? query [
        query: sqlform parameters query
    ]

    if verbose [
        print ["** SQL:" mold query]
        print ["** PARAMETERS:" mold parameters]
    ]

    ; !!! This INSERT takes a BLOCK!, not spread--this all ties into questions
    ; about the wisdom of reusing these verbs the way R3-Alpha did.
    ;
    return insert statement compose [(query) (spread parameters)]
]

export [odbc-execute]
