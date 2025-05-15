; datatypes/datatype.r

(not datatype? 1)

(datatype! = type of frame!)

(
    for-each 'typename [
        frame! blob! bitset! block! datatype! date! decimal!
        email! warning! file! handle! integer!
        issue! map! module! money! object! pair! group!
        path! percent! port! text! tag!
        time! tuple! parameter! url! word!
    ][
        if not datatype? get inside [] typename [
            panic [typename "is not a datatype!"]
        ]
    ]
    ok
)

(any-utf8?:type text!)
(not any-utf8?:type integer!)
(not try any-utf8?:type "abc")
(any-list?:type block!)
