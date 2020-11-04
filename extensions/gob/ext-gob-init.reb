REBOL [
    Title: "GOB! Extension"
    Name: Gob
    Type: Module
    Options: [isolate]
    Version: 1.0.0
    License: {Apache 2.0}

    Notes: {
        See %extensions/gob/README.md
    }
]

; !!! Should call UNREGISTER-GOB-HOOKS at some point (module finalizer?)
;
register-gob-hooks [  ; !!! See remarks, block of generics is vaporware ATM
    skip: generic [
        return: [<opt> gob!]
        series [<blank> gob!]
        offset [any-number! logic! pair!]
        /only
    ]

    at: generic [
        return: [<opt> gob!]
        series [<blank> gob!]
        index [any-number! logic! pair!]
        /only
    ]

    find: generic [
        return: [<opt> gob!]
        series [<blank> gob!]
        pattern [gob!]
    ]

    take*: generic [
        {Removes and returns one or more elements}

        return: [<opt> any-value!]
        series [gob!]
        /part [any-number! pair!]
        /deep
        /last
    ]

    insert: generic [
        return: [gob!]
        series [gob!]
        value [<opt> gob! block!]
        /part [any-number! pair!]
        /only
        /dup [any-number! pair!]
        /line
    ]

    append: generic [
        return: [gob!]
        series [gob!]
        value [<opt> gob! block!]
        /part [any-number! any-series! pair!]
        /only
        /dup [any-number! pair!]
        /line
    ]

    change: generic [
        return: [gob!]
        series [gob!]
        value [<opt> gob! block!]
        /part [any-number! any-series! pair!]
        /only
        /dup [any-number! pair!]
        /line
    ]

    remove: generic [
        return: [gob!]
        series [gob!]
        /part [any-number! any-series! pair! char!]
    ]

    clear: generic [
        series [gob!]
    ]

    swap: generic [
        series1 [gob!]
        series2 [gob!]
    ]

    reverse: generic [
        series [gob!]
        ; GOB! was not mentioned in the /PART refinement
    ]
]


sys/make-scheme [
    title: "GUI Events"
    name: 'event
    actor: system/modules/event/get-event-actor-handle
    awake: func [event] [
        print ["Default GUI event/awake:" event/type]
        true
    ]
]

sys/export []  ; current hacky mechanism is to put any exports here
