REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "REBOL 3 Mezzanine: Save"
    Rights: {
        Copyright 2012 REBOL Technologies
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Issues: {
        Is MOLD Missing a terminating newline? -CS
        Add MOLD/options -CS
    }
]


; Help try to avoid introducing CR into strings, and subvert the default
; checking on output that text does not contain CR bytes.
;
write-enlined: redescribe [
    {Write out a TEXT! with its LF sequences translated to CR LF}
](
    adapt :write [
        if not text? data [
            fail ["WRITE-ENLINED only works on TEXT! data"]
        ]
        data: as binary! enline copy data
    ]
)


mold64: func [
    "Temporary function to mold binary base 64." ; fix the need for this! -CS
    data
][
    let base: system.options.binary-base
    system.options.binary-base: 64
    let data: mold data
    system.options.binary-base: base
    return data
]

save: func [
    {Saves a value, block, or other data to a file, URL, binary, or text}

    ; !!! what RETURN values make sense?
    where "Where to save (suffix determines encoding)"
        [file! url! binary! text! blank!]
    value "Value(s) to save"
        [<const> element?]
    /header "Provide REBOL header block/object, or TRUE (header is in value)"
        [block! object! logic?]
    /all "Save in serialized format"
    /length "Save the length of the script content in the header"
    /compress "true = compressed, false = not, 'script = encoded string"
        [logic? word!]
][
    ; Recover common natives for words used as refinements.
    let all_SAVE: all
    all: runs :lib.all

    ; Special datatypes use codecs directly (e.g. PNG image file):
    all [
        not header  ; User wants to save value as script, not data file
        match [file! url!] where
        let type: file-type? where
        type <> 'rebol  ; handled by this routine, not by WRITE+ENCODE

        ; We have a codec.  Will check for valid type.
        return write where encode type :value
    ]

    any [length compress] then [  ; need header if compressed or lengthed
        header: default [[]]
    ]

    if header [
        if header = true [  ; the header is the first value in the block
            header: first ensure block! value
            value: my next
        ]

        ; Make header an object if it's not already
        ;
        header: if object? header [
            trim header  ; clean out words set to blank
        ] else [
            construct/only header  ; does not use STANDARD/HEADER
        ]

        ; Sync the header option with the /COMPRESS setting
        ;
        case [
            null? compress [
                compress: did find maybe (select header 'options) 'compress
            ]
            compress = false [
                remove find maybe select header 'options 'compress
            ]
            not block? select header 'options [
                append header spread compose [Options: (copy [compress])]
            ]
            not find select header 'options 'compress [
                append header.options 'compress
            ]
        ]

        if length [
            append header spread compose [
                length: #  ; "uses #, but any truthy value will work"
            ]
        ]

        length: ensure [~null~ integer!] try select header 'length
        header: body-of header
    ]

    ; !!! Maybe /all should be the default?  See #2159
    ;
    ; !!! This logic may look weird but it really was the original logic from
    ; R3-Alpha.  The idea was that if you SAVE'd a BLOCK! it assumed it was
    ; meant as saving the items in the block, but anything else was saved
    ; as is.  We have more options with isotopes now, but it's not clear
    ; what the scope of application of LOAD and SAVE really are.  Review.
    ;
    let data: either all_SAVE [
        if block? :value [
            mold/all spread value
        ] else [
            mold/all :value
        ]
    ][
        if block? :value [
            mold spread :value
        ] else [
            mold :value
        ]
    ]

    append data newline  ; MOLD does not append a newline

    case/all [
        let tmp: find maybe header 'checksum: [  ; e.g. says "checksum: true"
            ; Checksum uncompressed data, if requested
            change next tmp (checksum-core 'crc32 data)
        ]

        compress [
            ; Compress the data if necessary
            data: gzip data
        ]

        compress = 'script [
            data: mold64 data  ; File content is encoded as base-64
        ]

        not binary? data [
            data: to-binary data
        ]

        length [
            change next find header 'length: (length of data)
        ]

        header [
            insert data unspaced [{REBOL} _ (mold header) newline]
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

    return insert tail of where data  ; text! or binary!, insert data
]
