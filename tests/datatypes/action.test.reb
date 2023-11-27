; datatypes/action.r

(action? :abs)
(frame? unrun :abs)
(not frame? 1)
(isotope! = kind of :abs)
(frame! = kind of unrun :abs)

; frames are active
[#1659
    (1 == do reduce [unrun :abs -1])
]

; Actions should store labels of the last GET-WORD! or GET-PATH! that was
; used to retrieve them.  Using GET subverts changing the name.
[
    ('append = label of :append)
    ('append = label of :lib.append)

    (
        new-name: :append
        set 'old-name :append
        did all [
            'new-name = label of :new-name
            'new-name = label of get 'new-name
            'append = label of :old-name
            'append = label of get 'old-name
        ]
    )

    (
        set 'no-set-word func [] []
        null = label of get 'no-set-word
    )

    (
        f: make frame! unrun :append
        did all [
            'append = label of f
            'append = label of runs f
        ]
    )
]
