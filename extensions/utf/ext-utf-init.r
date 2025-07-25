Rebol [
    title: "UTF-16/etc. Codecs"

    name: UTF
    type: module

    options: []

    rights: --[
        Copyright 2012 REBOL Technologies
        Copyright 2012-2017 Ren-C Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    ]--

    license: --[
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    ]--
]

(sys.util/register-codec
    'text
    %.txt
    identify-text?/
    decode-text/
    encode-text/)

(sys.util/register-codec
    'utf-16le
    %.txt
    identify-utf16le?/
    decode-utf16le/
    encode-utf16le/)

(sys.util/register-codec
    'utf-16be
    %.txt
    identify-utf16be?/
    decode-utf16be/
    encode-utf16be/)
