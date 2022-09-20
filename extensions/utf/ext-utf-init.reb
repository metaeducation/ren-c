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
    reify :identify-text?
    reify :decode-text
    reify :encode-text)

(sys.util.register-codec*
    'utf-16le
    %.txt
    reify :identify-utf16le?
    reify :decode-utf16le
    reify :encode-utf16le)

(sys.util.register-codec*
    'utf-16be
    %.txt
    reify :identify-utf16be?
    reify :decode-utf16be
    reify :encode-utf16be)
