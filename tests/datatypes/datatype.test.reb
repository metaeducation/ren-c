; datatypes/datatype.r

(not type-word? 1)

(type-block! = type of frame!)

(
    for-each typename [
        frame! binary! bitset! block! type-word! date! decimal!
        email! error! file! get-tuple! get-word! handle! integer!
        issue! map! module! money! blank! object! pair! group!
        path! percent! port! set-tuple! set-word! text! tag!
        time! tuple! parameter! url! word!
    ][
        if not type-block? get inside [] typename [
            fail [typename "is not a type-block!"]
        ]
    ]
    ok
)

(type-word? &even?)  ; a type constraint

(type-word? logic?!)  ; a type constraint, not a datatype (for the moment...)
