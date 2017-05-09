REBOL [
    title: "ODBC Open Database Connectivity Scheme"

    name: odbc
    type: extension

    options: [extension delay]

    version: 0.6.0
    date: 24-01-2011

    author:  "Christian Ensel"
    rights:  "Copyright (C) 2010-2011 Christian Ensel"

    license: {
    This software is provided 'as-is', without any express or implied warranty.
    In no event will the author be held liable for any damages arising from the
    use of this software.

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in
    all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
    THE SOFTWARE.
    }
]

; These are the native coded support routines that are needed to be built from
; C in order to interface with ODBC.  The scheme code is the usermode support
; to provide a higher level interface.
;
; open-connection: native [connection [object!] spec [string!]]
; open-statement: native [connection [object!] statement [object!]]
; insert-odbc: native [statement [object!] sql [block!]]
; copy-odbc: native [statement [object!] length [integer!]]
; close-statement: native [statement [object!]]
; close-connection: native [connection [object!]]
; update-odbc: native [connection [object!] access [logic!] commit [logic!]]


database-prototype: context [
    henv: _ ; SQLHENV handle!
    hdbc: _ ; SQLHDBC handle!
    statements:  [] ; statement objects
]

statement-prototype: context [
    database: ;
    hstmt: _ ; SQLHSTMT
    string: _
    titles: _
    columns: _
]

sys/make-scheme [
    name:  'odbc
    title: "ODBC Open Database Connectivity Scheme"

    actor: context [
        open: function [
            {Open a database port}
            port [port!]
                {WORD! spec then assume DSN, else BLOCK! DSN-less datasource}
        ][
            port/state: context [
                access: 'write
                commit: 'auto
            ]

            port/locals: make database-prototype []

            result: open-connection port/locals case [
                string? spec: select port/spec 'target [spec]
                string? spec: select port/spec 'host [unspaced ["dsn=" spec]]

                cause-error 'access 'invalid-spec port/spec
            ]

            port
        ]

        pick*: function [
            port [port!]
            index
                {Index to pick from (only supports 1, for FIRST)}
        ][
            statement: make statement-prototype []

            database: statement/database: port/locals

            open-statement database statement

            port: lib/open port/spec/ref
            port/locals: statement

            append database/statements port

            port
        ]

        update: function [port [port!]] [
            if get in connection: port/locals 'hdbc [
                (update-odbc
                    connection
                    port/state/access = 'write
                    port/state/commit = 'auto
                )
                return port
            ]
        ]

        close: procedure [
            {Closes a statement port only or a database port w/all statements}
            port [port!]
        ][
            if get in (statement: port/locals) 'hstmt [
                remove find head statement/database/statements port
                close-statement statement
                leave
            ]

            if get in (connection: port/locals) 'hdbc [
                for-each stmt-port connection/statements [close stmt-port]
                clear connection/statements
                close-connection connection
                leave
            ]
        ]

        insert: function [
            port [port!]
            sql [string! word! block!]
                {SQL statement or catalog, parameter blocks are reduced first}
        ][
            insert-odbc port/locals reduce compose [(sql)]
        ]

        copy: function [port [port!] /part length [integer!]] [
            if not part [
                length: blank
            ]
            copy-odbc port/locals length
        ]
    ]
]


comment [
    a: b: c: 0
    dt [loop 512 [
        cache: open odbc://cachesamples
        a: a + 1
        close cache
        b: b + 1
    ]]
]

comment [
    a: b: c: 0
    dt [loop 512 [
        cache: open odbc://cachesamples
        a: a + 1
        db: first cache
        b: b + 1
        close cache
        c: c + 1
    ]]
]

comment [
    dbs: []
    a: 0
    dt [
        cache: open odbc://cachesamples
        loop 512 [append dbs first cache | a: a + 1]
        close cache
    ]
]

comment [
    a: b: c: 0
    dt [loop 512 [
        postgresql: open odbc://pgsamples
        a: a + 1
        close postgresql
        b: b + 1
    ]]
]

comment [
    a: b: c: 0
    dt [loop 512 [
        postgresql: open odbc://pgsamples
        a: a + 1
        db: first postgresql
        b: b + 1
        close postgresql
        c: c + 1
    ]]
]
