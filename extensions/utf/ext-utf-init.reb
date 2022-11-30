REBOL [
    Title: "UTF-16/etc. Codecs"

    Name: UTF
    Type: Module

    Options: []

    Rights: {
        Copyright 2012 REBOL Technologies
        Copyright 2012-2017 Ren-C Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
]

(sys.util.register-codec*
    'text
    %.txt
    unrun :identify-text?
    unrun :decode-text
    unrun :encode-text)

(sys.util.register-codec*
    'utf-16le
    %.txt
    unrun :identify-utf16le?
    unrun :decode-utf16le
    unrun :encode-utf16le)

(sys.util.register-codec*
    'utf-16be
    %.txt
    unrun :identify-utf16be?
    unrun :decode-utf16be
    unrun :encode-utf16be)
