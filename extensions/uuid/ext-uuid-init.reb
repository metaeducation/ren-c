REBOL [
    Title: "UUID Extension"
    Name: UUID
    Type: Module
    Options: [isolate]
    Version: 1.0.0
    License: {Apache 2.0}
]

to-text: function [
    "Convert the UUID to the text string form ({8-4-4-4-12})"
    uuid [binary!]
][
    delimit "-" map-each/only w reduce [
        copy/part uuid 4
        copy/part (skip uuid 4) 2
        copy/part (skip uuid 6) 2
        copy/part (skip uuid 8) 2
        copy/part (skip uuid 10) 6
    ][
        enbase/base w 16
    ]
]
