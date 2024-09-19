; datatypes/action.r

(action? :abs)
(frame? unrun :abs)
(not frame? 1)
(antiform! = type of :abs)
(frame! = type of unrun :abs)

; frames are active
[#1659
    (1 == eval reduce [unrun :abs -1])
]

; Actions should store labels of the last GET-WORD! or GET-TUPLE! that was
; used to retrieve them.  Using GET subverts changing the name.
[
    ('append = label of get $append)
    ('append = label of get $lib/append)

    (
        new-name: get $append
        set $old-name get $append
        all [
            'new-name = label of get $new-name
            'append = label of get $old-name
        ]
    )

    (
        set $no-set-word func [] []
        null = label of get $no-set-word
    )

    (
        f: make frame! unrun :append
        all [
            'append = label of f
            'append = label of runs f
        ]
    )
]
