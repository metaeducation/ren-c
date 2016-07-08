; functions/control/all.r
; zero values
[true == all []]
; one value
[:abs = all [:abs]]
[
    a-value: #{}
    same? a-value all [a-value]
]
[
    a-value: charset ""
    same? a-value all [a-value]
]
[
    a-value: []
    same? a-value all [a-value]
]
[
    a-value: blank!
    same? a-value all [a-value]
]
[1/Jan/0000 = all [1/Jan/0000]]
[0.0 == all [0.0]]
[1.0 == all [1.0]]
[
    a-value: me@here.com
    same? a-value all [a-value]
]
[error? all [try [1 / 0]]]
[
    a-value: %""
    same? a-value all [a-value]
]
[
    a-value: does []
    same? :a-value all [:a-value]
]
[
    a-value: first [:a]
    :a-value == all [:a-value]
]
[#"^@" == all [#"^@"]]
[
    a-value: make image! 0x0
    same? a-value all [a-value]
]
[0 == all [0]]
[1 == all [1]]
[#a == all [#a]]
[
    a-value: first ['a/b]
    :a-value == all [:a-value]
]
[
    a-value: first ['a]
    :a-value == all [:a-value]
]
[true = all [true]]
[blank? all [false]]
[$1 == all [$1]]
[same? :type-of all [:type-of]]
[blank? all [_]]
[
    a-value: make object! []
    same? :a-value all [:a-value]
]
[
    a-value: first [()]
    same? :a-value all [:a-value]
]
[same? get '+ all [get '+]]
[0x0 == all [0x0]]
[
    a-value: 'a/b
    :a-value == all [:a-value]
]
[
    a-value: make port! http://
    port? all [:a-value]
]
[/a == all [/a]]
[
    a-value: first [a/b:]
    :a-value == all [:a-value]
]
[
    a-value: first [a:]
    :a-value == all [:a-value]
]
[
    a-value: ""
    same? :a-value all [:a-value]
]
[
    a-value: make tag! ""
    same? :a-value all [:a-value]
]
[0:00 == all [0:00]]
[0.0.0 == all [0.0.0]]
[true == all [()]] ;-- Ren/C change
['a == all ['a]]
; two values
[:abs = all [true :abs]]
[
    a-value: #{}
    same? a-value all [true a-value]
]
[
    a-value: charset ""
    same? a-value all [true a-value]
]
[
    a-value: []
    same? a-value all [true a-value]
]
[
    a-value: blank!
    same? a-value all [true a-value]
]
[1/Jan/0000 = all [true 1/Jan/0000]]
[0.0 == all [true 0.0]]
[1.0 == all [true 1.0]]
[
    a-value: me@here.com
    same? a-value all [true a-value]
]
[error? all [true try [1 / 0]]]
[
    a-value: %""
    same? a-value all [true a-value]
]
[
    a-value: does []
    same? :a-value all [true :a-value]
]
[
    a-value: first [:a]
    same? :a-value all [true :a-value]
]
[#"^@" == all [true #"^@"]]
[
    a-value: make image! 0x0
    same? a-value all [true a-value]
]
[0 == all [true 0]]
[1 == all [true 1]]
[#a == all [true #a]]
[
    a-value: first ['a/b]
    :a-value == all [true :a-value]
]
[
    a-value: first ['a]
    :a-value == all [true :a-value]
]
[$1 == all [true $1]]
[same? :type-of all [true :type-of]]
[blank? all [true _]]
[
    a-value: make object! []
    same? :a-value all [true :a-value]
]
[
    a-value: first [()]
    same? :a-value all [true :a-value]
]
[same? get '+ all [true get '+]]
[0x0 == all [true 0x0]]
[
    a-value: 'a/b
    :a-value == all [true :a-value]
]
[
    a-value: make port! http://
    port? all [true :a-value]
]
[/a == all [true /a]]
[
    a-value: first [a/b:]
    :a-value == all [true :a-value]
]
[
    a-value: first [a:]
    :a-value == all [true :a-value]
]
[
    a-value: ""
    same? :a-value all [true :a-value]
]
[
    a-value: make tag! ""
    same? :a-value all [true :a-value]
]
[0:00 == all [true 0:00]]
[0.0.0 == all [true 0.0.0]]
[true == all [1020 ()]] ;-- Ren/C change
['a == all [true 'a]]
[true = all [:abs true]]
[
    a-value: #{}
    true = all [a-value true]
]
[
    a-value: charset ""
    true = all [a-value true]
]
[
    a-value: []
    true = all [a-value true]
]
[
    a-value: blank!
    true = all [a-value true]
]
[true = all [1/Jan/0000 true]]
[true = all [0.0 true]]
[true = all [1.0 true]]
[
    a-value: me@here.com
    true = all [a-value true]
]
[true = all [try [1 / 0] true]]
[
    a-value: %""
    true = all [a-value true]
]
[
    a-value: does []
    true = all [:a-value true]
]
[
    a-value: first [:a]
    true = all [:a-value true]
]
[true = all [#"^@" true]]
[
    a-value: make image! 0x0
    true = all [a-value true]
]
[true = all [0 true]]
[true = all [1 true]]
[true = all [#a true]]
[
    a-value: first ['a/b]
    true = all [:a-value true]
]
[
    a-value: first ['a]
    true = all [:a-value true]
]
[true = all [true true]]
[blank? all [false true]]
[blank? all [true false]]
[true = all [$1 true]]
[true = all [:type-of true]]
[blank? all [_ true]]
[
    a-value: make object! []
    true = all [:a-value true]
]
[
    a-value: first [()]
    true = all [:a-value true]
]
[true = all [get '+ true]]
[true = all [0x0 true]]
[
    a-value: 'a/b
    true = all [:a-value true]
]
[
    a-value: make port! http://
    true = all [:a-value true]
]
[true = all [/a true]]
[
    a-value: first [a/b:]
    true = all [:a-value true]
]
[
    a-value: first [a:]
    true = all [:a-value true]
]
[
    a-value: ""
    true = all [:a-value true]
]
[
    a-value: make tag! ""
    true = all [:a-value true]
]
[true = all [0:00 true]]
[true = all [0.0.0 true]]
[true = all [() true]]
[true = all ['a true]]
; evaluation stops after encountering FALSE or NONE
[
    success: true
    all [false success: false]
    success
]
[
    success: true
    all [blank success: false]
    success
]
; evaluation continues otherwise
[
    success: false
    all [true success: true]
    success
]
[
    success: false
    all [1 success: true]
    success
]
; RETURN stops evaluation
[
    f1: does [all [return 1 2] 2]
    1 = f1
]
; THROW stops evaluation
[
    1 = catch [
        all [
            throw 1
            2
        ]
    ]
]
; BREAK stops evaluation
[
    1 = loop 1 [
        all [
            break/return 1
            2
        ]
    ]
]
; recursivity
[all [true all [true]]]
[not all [true all [false]]]
; infinite recursion
[
    blk: [all blk]
    error? try blk
]
