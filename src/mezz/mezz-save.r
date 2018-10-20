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

mold64: function [
    "Temporary function to mold binary base 64." ; fix the need for this! -CS
    data
][
    base: system/options/binary-base
    system/options/binary-base: 64
    data: mold :data
    system/options/binary-base: :base
    data
]

save: function [
    {Saves a value, block, or other data to a file, URL, binary, or text.}
    where [file! url! binary! text! blank!]
        {Where to save (suffix determines encoding)}
    value {Value(s) to save}
    /header
        {Provide a REBOL header block (or output non-code datatypes)}
    header-data [block! object! logic!]
        {Header block, object, or TRUE (header is in value)}
    /all ;-- renamed to `all_SAVE` to avoid ambiguity with native
        {Save in serialized format}
    /length
        {Save the length of the script content in the header}
    /compress
        {Save in a compressed format or not}
    method [logic! word!]
        {true = compressed, false = not, 'script = encoded string}
][
    ; Recover common natives for words used as refinements.
    all_SAVE: all
    all: :lib/all

    method: default [_]
    header-data: default [_]

    ;-- Special datatypes use codecs directly (e.g. PNG image file):
    all [
        not header ; User wants to save value as script, not data file
        match [file! url!] where
        type: try file-type? where
        type isn't 'Rebol ;-- handled by this routine, not by WRITE+ENCODE
    ] then [
        ; We have a codec.  Will check for valid type.
        return write where encode type :value
    ]

    ;-- Compressed scripts and script lengths require a header:
    any [length method] then [
        header: true
        header-data: default [[]]
    ]

    ;-- Handle the header object:
    if header-data [

        ;-- #[true] indicates the header is the first value in the block
        if header-data == true [
            header-data: first ensure block! value
            value: my next ;-- do not use TAKE (leave header in position)
        ]

        ;; Make it an object if it's not already
        ;;
        header-data: if object? :header-data [
            trim :header-data ;; clean out words set to blank
        ] else [
            has/only :header-data ;; does not use STANDARD/HEADER
        ]

        if compress [ ; Make the header option match
            case [
                not method [
                    remove find to-value select header-data 'options 'compress
                ]
                not block? select header-data 'options [
                    join header-data ['options copy [compress]]
                ]
                not find header-data/options 'compress [
                    append header-data/options 'compress
                ]
            ]
        ]

        if length [
            ; any truthy value will work, but this uses #[true].
            ;
            append header-data compose [length: (true)]
        ]

        compress: did find try (select header-data 'options) 'compress
        if not compress [
            method: _
        ]

        length: ensure [integer! blank!] try select header-data 'length
        header-data: body-of header-data
    ]

    ; !!! Maybe /all should be the default?  See #2159
    data: either all_SAVE [mold/all/only :value] [
        mold/only :value
    ]

    ; mold does not append a newline? Nope.
    append data newline

    case/all [
        tmp: try find header-data 'checksum [
            ; Checksum uncompressed data, if requested
            change next tmp checksum/secure data: to-binary data
        ]

        compress [
            ; Compress the data if necessary
            data: gzip data
        ]

        method is 'Script [
            data: mold64 data ; File content is encoded as base-64
        ]

        not binary? data [
            data: to-binary data
        ]

        length [
            change find/tail header-data 'length (length of data)
        ]

        header-data [
            insert data unspaced [{REBOL} space (mold header-data) newline]
        ]
    ]

    case [
        file? where [
            ; WRITE converts to UTF-8, saves overhead
            write where data
        ]

        url? where [
            ; !!! Comment said "But some schemes don't support it"
            ; Presumably saying that the URL scheme does not support UTF-8 (?)
            write where data
        ]

        blank? where [
            ; just return the UTF-8 binary
            data
        ]
    ] else [
        ; text! or binary!, insert data
        insert tail of where data
    ]
]
