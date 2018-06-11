REBOL [
    title: "ØMQ extension"

    name: ZeroMQ
    type: extension

    options: [extension delay]

    version: 0.2.0
    date: 24-01-2011

    author:  "Andreas Bolka"
    rights:  "Copyright (C) 2011 Andreas Bolka <a AT bolka DOT at>"

    license: {
        Licensed under the terms of the Apache License, Version 2.0

        The zmqext REBOL 3 extension uses the ØMQ library, the use of which is
        granted under the terms of the GNU Lesser General Public License
        (LGPL), Version 3.
    }
]

; !!! The Rebol portion of the original 0MQ extension only contained a MAP!
; of ZeroMQ constants.  Generally speaking these constants have been folded
; into the exported actions as WORD!s, with the translation to integer done
; in the code.
