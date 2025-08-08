; datatypes/action.r

(action? abs/)
(frame? unrun abs/)
(not frame? 1)
(action! = type of abs/)
(frame! = type of unrun abs/)

; frames are active
[#1659
    (1 = eval reduce [unrun abs/ -1])
]

; Actions should store labels of the last GET-WORD! or GET-TUPLE! that was
; used to retrieve them.  Using GET subverts changing the name.
[
    ('append = label of append/)
    ('append = label of lib.append/)

    (
        new-name: append/
        ^old-name: append/
        all [
            'new-name = label of new-name/
            'append = label of old-name/
        ]
    )

    (
        ^anonymous: func [] []
        null = label of anonymous/
    )

    (
        f: make frame! append/
        all [
            'append = label of f
            'append = label of runs f
        ]
    )
]
