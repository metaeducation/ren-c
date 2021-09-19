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

(sys.register-codec*
    'text
    %.txt
    :identify-text?
    :decode-text
    :encode-text)

(sys.register-codec*
    'utf-16le
    %.txt
    :identify-utf16le?
    :decode-utf16le
    :encode-utf16le)

(sys.register-codec*
    'utf-16be
    %.txt
    :identify-utf16be?
    :decode-utf16be
    :encode-utf16be)
