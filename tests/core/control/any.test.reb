; functions/control/any.r
; zero values
[blank? any []]
; one value
[:abs = any [:abs]]
[
    a-value: #{}
    same? a-value any [a-value]
]
[
    a-value: charset ""
    same? a-value any [a-value]
]
[
    a-value: []
    same? a-value any [a-value]
]
[
    a-value: blank!
    same? a-value any [a-value]
]
[1/Jan/0000 = any [1/Jan/0000]]
[0.0 == any [0.0]]
[1.0 == any [1.0]]
[
    a-value: me@here.com
    same? a-value any [a-value]
]
[error? any [try [1 / 0]]]
[
    a-value: %""
    same? a-value any [a-value]
]
[
    a-value: does []
    same? :a-value any [:a-value]
]
[
    a-value: first [:a]
    :a-value == any [:a-value]
]
[#"^@" == any [#"^@"]]
[
    a-value: make image! 0x0
    same? a-value any [a-value]
]
[0 == any [0]]
[1 == any [1]]
[#a == any [#a]]
[
    a-value: first ['a/b]
    :a-value == any [:a-value]
]
[
    a-value: first ['a]
    :a-value == any [:a-value]
]
[true = any [true]]
[blank? any [false]]
[$1 == any [$1]]
[same? :type-of any [:type-of]]
[blank? any [_]]
[
    a-value: make object! []
    same? :a-value any [:a-value]
]
[
    a-value: first [()]
    same? :a-value any [:a-value]
]
[same? get '+ any [get '+]]
[0x0 == any [0x0]]
[
    a-value: 'a/b
    :a-value == any [:a-value]
]
[
    a-value: make port! http://
    port? any [:a-value]
]
[/a == any [/a]]
; routine test?
[
    a-value: first [a/b:]
    :a-value == any [:a-value]
]
[
    a-value: first [a:]
    :a-value == any [:a-value]
]
[
    a-value: ""
    same? :a-value any [:a-value]
]
[
    a-value: make tag! ""
    same? :a-value any [:a-value]
]
[0:00 == any [0:00]]
[0.0.0 == any [0.0.0]]
[blank? any [()]]
['a == any ['a]]
; two values
[:abs = any [false :abs]]
[
    a-value: #{}
    same? a-value any [false a-value]
]
[
    a-value: charset ""
    same? a-value any [false a-value]
]
[
    a-value: []
    same? a-value any [false a-value]
]
[
    a-value: blank!
    same? a-value any [false a-value]
]
[1/Jan/0000 = any [false 1/Jan/0000]]
[0.0 == any [false 0.0]]
[1.0 == any [false 1.0]]
[
    a-value: me@here.com
    same? a-value any [false a-value]
]
[error? any [false try [1 / 0]]]
[
    a-value: %""
    same? a-value any [false a-value]
]
[
    a-value: does []
    same? :a-value any [false :a-value]
]
[
    a-value: first [:a]
    :a-value == any [false :a-value]
]
[#"^@" == any [false #"^@"]]
[
    a-value: make image! 0x0
    same? a-value any [false a-value]
]
[0 == any [false 0]]
[1 == any [false 1]]
[#a == any [false #a]]
[
    a-value: first ['a/b]
    :a-value == any [false :a-value]
]
[
    a-value: first ['a]
    :a-value == any [false :a-value]
]
[true = any [false true]]
[blank? any [false false]]
[$1 == any [false $1]]
[same? :type-of any [false :type-of]]
[blank? any [false _]]
[
    a-value: make object! []
    same? :a-value any [false :a-value]
]
[
    a-value: first [()]
    same? :a-value any [false :a-value]
]
[same? get '+ any [false get '+]]
[0x0 == any [false 0x0]]
[
    a-value: 'a/b
    :a-value == any [false :a-value]
]
[
    a-value: make port! http://
    port? any [false :a-value]
]
[/a == any [false /a]]
[
    a-value: first [a/b:]
    :a-value == any [false :a-value]
]
[
    a-value: first [a:]
    :a-value == any [false :a-value]
]
[
    a-value: ""
    same? :a-value any [false :a-value]
]
[
    a-value: make tag! ""
    same? :a-value any [false :a-value]
]
[0:00 == any [false 0:00]]
[0.0.0 == any [false 0.0.0]]
[blank? any [false ()]]
['a == any [false 'a]]
[:abs = any [:abs false]]
[
    a-value: #{}
    same? a-value any [a-value false]
]
[
    a-value: charset ""
    same? a-value any [a-value false]
]
[
    a-value: []
    same? a-value any [a-value false]
]
[
    a-value: blank!
    same? a-value any [a-value false]
]
[1/Jan/0000 = any [1/Jan/0000 false]]
[0.0 == any [0.0 false]]
[1.0 == any [1.0 false]]
[
    a-value: me@here.com
    same? a-value any [a-value false]
]
[error? any [try [1 / 0] false]]
[
    a-value: %""
    same? a-value any [a-value false]
]
[
    a-value: does []
    same? :a-value any [:a-value false]
]
[
    a-value: first [:a]
    :a-value == any [:a-value false]
]
[#"^@" == any [#"^@" false]]
[
    a-value: make image! 0x0
    same? a-value any [a-value false]
]
[0 == any [0 false]]
[1 == any [1 false]]
[#a == any [#a false]]
[
    a-value: first ['a/b]
    :a-value == any [:a-value false]
]
[
    a-value: first ['a]
    :a-value == any [:a-value false]
]
[true = any [true false]]
[$1 == any [$1 false]]
[same? :type-of any [:type-of false]]
[blank? any [_ false]]
[
    a-value: make object! []
    same? :a-value any [:a-value false]
]
[
    a-value: first [()]
    same? :a-value any [:a-value false]
]
[same? get '+ any [get '+ false]]
[0x0 == any [0x0 false]]
[
    a-value: 'a/b
    :a-value == any [:a-value false]
]
[
    a-value: make port! http://
    port? any [:a-value false]
]
[/a == any [/a false]]
[
    a-value: first [a/b:]
    :a-value == any [:a-value false]
]
[
    a-value: first [a:]
    :a-value == any [:a-value false]
]
[
    a-value: ""
    same? :a-value any [:a-value false]
]
[
    a-value: make tag! ""
    same? :a-value any [:a-value false]
]
[0:00 == any [0:00 false]]
[0.0.0 == any [0.0.0 false]]
[blank? any [() false]]
['a == any ['a false]]
; evaluation stops after encountering something else than FALSE or NONE
[
    success: true
    any [true success: false]
    success
]
[
    success: true
    any [1 success: false]
    success
]
; evaluation continues otherwise
[
    success: false
    any [false success: true]
    success
]
[
    success: false
    any [blank success: true]
    success
]
; RETURN stops evaluation
[
    f1: does [any [return 1 2] 2]
    1 = f1
]
; THROW stops evaluation
[
    1 = catch [
        any [
            throw 1
            2
        ]
    ]
]
; BREAK stops evaluation
[
    1 = loop 1 [
        any [
            break/return 1
            2
        ]
    ]
]
; recursivity
[any [false any [true]]]
[blank? any [false any [false]]]
; infinite recursion
[
    blk: [any blk]
    error? try blk
]
