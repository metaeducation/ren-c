Rebol [
    system: "Rebol [R3] Language Interpreter and Run-time Environment"
    title: "REBOL 3 Mezzanine: Save"
    rights: {
        Copyright 2012 REBOL Technologies
        REBOL is a trademark of REBOL Technologies
    }
    license: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    issues: {
        Is MOLD Missing a terminating newline? -CS
        Add MOLD/options -CS
    }
]

mold64: function [
    "Temporary function to mold binary base 64." ; fix the need for this! -CS
    data
    <local> molded
][
    molded: enbase data  ; default
    insert molded "64#{"
    append molded "}"
    return molded
]

save: function [
    {Saves a value, block, or other data to a file, URL, binary, or text.}
    where [file! url! binary! text! blank!]
        {Where to save (suffix determines encoding)}
    value {Value(s) to save}
    /header
        {Provide a REBOL header block (or output non-code datatypes)}
    header-data [block! object! word!]
        {Header block, object, or INCLUDED (header is in value)}
][
    header-data: default [null]

    ;-- Special datatypes use codecs directly (e.g. PNG image file):
    all [
        not header ; User wants to save value as script, not data file
        match [file! url!] where
        type: file-type? where
        type <> 'rebol ;-- handled by this routine, not by WRITE+ENCODE
    ] then [
        ; We have a codec.  Will check for valid type.
        return write where encode type :value
    ]

    ;-- Handle the header object:
    if header-data [

        ;-- INCLUDED indicates the header is the first value in the block
        if header-data = 'included [
            header-data: first ensure block! value
            value: my next ;-- do not use TAKE (leave header in position)
        ]
        if word? header-data [
            panic "INCLUDED is the only valid WORD! for SAVE HEADER-DATA"
        ]

        ;; Make it an object if it's not already
        ;;
        header-data: if object? :header-data [
            trim :header-data ;; clean out words set to blank
        ] else [
            make object! :header-data ;; does not use STANDARD/HEADER
        ]

        header-data: body-of header-data
    ]

    data: mold/only :value

    ; mold does not append a newline? Nope.
    append data newline

    case/all [
        not binary? data [
            data: to-binary data
        ]

        header-data [
            insert data unspaced [{REBOL} space (mold header-data) newline]
        ]
    ]

    return case [
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
