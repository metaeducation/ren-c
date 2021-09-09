; datatypes/action.r

(action? :abs)
(not action? 1)
(action! = type of :abs)
; actions are active
[#1659
    (1 == do reduce [:abs -1])
]

; Actions should store labels of the last GET-WORD! or GET-PATH! that was
; used to retrieve them.  Using GET subverts changing the name.
[
    ('append = label of :append)
    ('append = label of :lib/append)

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
        f: make frame! :append
        did all [
            'append = label of f
            'append = label of make action! f
        ]
    )
]
