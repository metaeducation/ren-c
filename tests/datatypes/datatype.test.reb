; datatypes/datatype.r

(not type-word? 1)

(type-block! = type of frame!)

(
    for-each 'typename [
        frame! blob! bitset! block! type-word! date! decimal!
        email! error! file! handle! integer!
        issue! map! module! money! blank! object! pair! group!
        path! percent! port! text! tag!
        time! tuple! parameter! url! word!
    ][
        if not type-block? get inside [] typename [
            fail [typename "is not a type-block!"]
        ]
    ]
    ok
)

(type-word? &even?)  ; a type constraint

(any-utf8?:type text!)
(not any-utf8?:type integer!)
(not try any-utf8?:type "abc")
(any-list?:type block!)
