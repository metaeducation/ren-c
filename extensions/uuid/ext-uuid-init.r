Rebol [
    title: "UUID Extension"
    name: UUID
    type: module
    version: 1.0.0
    license: "Apache 2.0"
]

to-text: func [
    "Convert the UUID to the text string form ({8-4-4-4-12})"
    uuid [blob!]
][
    return delimit "-" map-each 'w reduce [
        copy:part uuid 4
        copy:part (skip uuid 4) 2
        copy:part (skip uuid 6) 2
        copy:part (skip uuid 8) 2
        copy:part (skip uuid 10) 6
    ][
        enbase:base w 16
    ]
]
