Rebol [
    system: "Rebol [R3] Language Interpreter and Run-time Environment"
    title: "REBOL 3 Mezzanine: Save"
    rights: --{
        Copyright 2012 REBOL Technologies
        REBOL is a trademark of REBOL Technologies
    }--
    license: --{
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }--
]


; Help try to avoid introducing CR into strings, and subvert the default
; checking on output that text does not contain CR bytes.
;
/write-enlined: redescribe [
    "Write out a TEXT! with its LF sequences translated to CR LF"
](
    adapt write/ [
        if not text? data [
            fail ["WRITE-ENLINED only works on TEXT! data"]
        ]
        data: as blob! enline copy data
    ]
)


mold64: func [
    "Temporary function to mold binary base 64." ; fix the need for this! -CS
    data
][
    let molded: enbase data  ; default
    insert molded "64#{"
    append molded "}"
    return molded
]

; 1. Script compression was a weird feature that is not a priority in Ren-C,
;    but keeping things working well enough to run the tests helps expose
;    thinking points.
;
save: func [
    "Saves a value, block, or other data to a file, URL, binary, or text"

    ; !!! what RETURN values make sense?
    where "Where to save (suffix determines encoding)"
        [file! url! blob! text! blank!]
    value "Value(s) to save"
        [<const> element?]
    :header "Provide REBOL header block/object, or INCLUDED if in value"
        [block! object! 'included]
    :length "Save the length of the script content in the header"
    :compress "Detect from header if not supplied"  ; weird old feature [1]
        ['none 'raw 'base64]
][
    ; Special datatypes use codecs directly (e.g. PNG image file):
    all [
        not header  ; User wants to save value as script, not data file
        match [file! url!] where
        let type: file-type? where
        type <> 'rebol  ; handled by this routine, not by WRITE+ENCODE

        ; We have a codec.  Will check for valid type.
        return write where encode type value
    ]

    any [
        length
        (compress <> 'none) and (compress <> null)
    ] then [  ; need header if compressed or lengthed
        header: default [copy []]
    ]

    if header [
        if header = 'included [  ; the header is the first value in the block
            header: first ensure block! value
            value: my next
        ]

        ; Make header an object if it's not already
        ;
        header: if object? header [
            trim header  ; clean out words set to blank
        ] else [
            construct (inert header)  ; does not use STANDARD.HEADER
        ]

        ; Sync the header option with the :COMPRESS setting
        ;
        case [
            null? compress [
                compress: all [
                    find maybe (select header 'options) 'compress
                    'blob  ; I guess this is the default?  [1]
                ]
            ]
            compress = 'none [
                remove find maybe select header 'options 'compress
            ]
            not block? select header 'options [
                extend header [Options: '[compress]]
            ]
            not find select header 'options 'compress [
                append header.options 'compress
            ]
        ]

        if length [
            extend header [
                length: #  ; "uses #, but any truthy value will work"
            ]
        ]

        length: ensure [~null~ integer!] try select header 'length
        header: map-each [key val] header [
            assert [not antiform? val]
            spread [setify key val]
        ]
    ]

    compress: default ['none]

    if not block? value [
        fail "Rebol code passed to SAVE must be BLOCK! (LOAD only gives block)"
    ]
    let data: mold spread value

    append data newline  ; MOLD does not append a newline

    case:all [
        let tmp: find maybe header 'checksum: [  ; e.g. says "checksum: true"
            ; Checksum uncompressed data, if requested
            change next tmp (checksum-core 'crc32 data)
        ]

        compress <> 'none [
            data: gzip data
        ]

        compress = 'base64 [
            data: mold64 data
        ]

        assert [not blob? data]
        elide [data: as binary! data]

        length [
            change next find header 'length: (length of data)
        ]

        header [
            insert data unspaced ["REBOL" _ (mold header) newline]
        ]
    ]

    case [
        file? where [
            return write where data
        ]

        url? where [
            ; !!! Comment said "But some schemes don't support it"
            ; Presumably saying that the URL scheme does not support UTF-8 (?)
            return write where data
        ]

        blank? where [
            return data  ; just return the UTF-8 binary
        ]
    ]

    return insert tail of where data  ; text! or blob!, insert data
]
