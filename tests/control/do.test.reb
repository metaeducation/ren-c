; functions/control/do.r
(
    success: false
    do [success: true]
    success
)
(1 == eval :abs -1)
(
    a-value: to binary! "1 + 1"
    2 == do a-value
)
(
    a-value: charset ""
    same? a-value eval a-value
)
; do block start
(void? do [])
(:abs = do [:abs])
(
    a-value: #{}
    same? a-value do reduce [a-value]
)
(
    a-value: charset ""
    same? a-value do reduce [a-value]
)
(
    a-value: []
    same? a-value do reduce [a-value]
)
(same? blank! do reduce [blank!])
(1/Jan/0000 = do [1/Jan/0000])
(0.0 == do [0.0])
(1.0 == do [1.0])
(
    a-value: me@here.com
    same? a-value do reduce [a-value]
)
(error? do [try [1 / 0]])
(
    a-value: %""
    same? a-value do reduce [a-value]
)
(
    a-value: does []
    same? :a-value do [:a-value]
)
(
    a-value: first [:a-value]
    :a-value == do reduce [:a-value]
)
(#"^@" == do [#"^@"])
(
    a-value: make image! 0x0
    same? a-value do reduce [a-value]
)
(0 == do [0])
(1 == do [1])
(#a == do [#a])
(
    a-value: first ['a/b]
    :a-value == do [:a-value]
)
(
    a-value: first ['a]
    :a-value == do [:a-value]
)
(#[true] == do [#[true]])
(#[false] == do [#[false]])
($1 == do [$1])
(same? :append do [:append])
(blank? do [_])
(
    a-value: make object! []
    same? :a-value do reduce [:a-value]
)
(
    a-value: first [()]
    same? :a-value do [:a-value]
)
(same? get '+ do [get '+])
(0x0 == do [0x0])
(
    a-value: 'a/b
    :a-value == do [:a-value]
)
(
    a-value: make port! http://
    port? do reduce [:a-value]
)
(/a == do [/a])
(
    a-value: first [a/b:]
    :a-value == do [:a-value]
)
(
    a-value: first [a:]
    :a-value == do [:a-value]
)
(
    a-value: ""
    same? :a-value do reduce [:a-value]
)
(
    a-value: make tag! ""
    same? :a-value do reduce [:a-value]
)
(0:00 == do [0:00])
(0.0.0 == do [0.0.0])
(void? do [()])
('a == do ['a])
; do block end
(
    a-value: blank!
    same? a-value eval a-value
)
(1/Jan/0000 == eval 1/Jan/0000)
(0.0 == eval 0.0)
(1.0 == eval 1.0)
(
    a-value: me@here.com
    same? a-value eval a-value
)
(error? try [do try [1 / 0] 1])
(
    a-value: does [5]
    5 == eval :a-value
)
(
    a: 12
    a-value: first [:a]
    :a == eval :a-value
)
(#"^@" == eval #"^@")
(
    a-value: make image! 0x0
    same? a-value eval a-value
)
(0 == eval 0)
(1 == eval 1)
(#a == eval #a)
;-- CC#2101, #1434
(
    a-value: first ['a/b]
    all [
        lit-path? a-value
        path? eval :a-value
        (to-path :a-value) == (eval :a-value)
    ]
)
(
    a-value: first ['a]
    all [
        lit-word? a-value
        word? eval :a-value
        (to-word :a-value) == (eval :a-value)
    ]
)
(true = eval true)
(false = eval false)
($1 == eval $1)
(_ = eval (specialize 'of [property: 'type]) ())
(blank? do _)
(
    a-value: make object! []
    same? :a-value eval :a-value
)
(
    a-value: first [(2)]
    2 == do as block! :a-value
)
(
    a-value: 'a/b
    a: make object! [b: 1]
    1 == eval :a-value
)
(
    a-value: make port! http://
    port? eval :a-value
)
(
    a-value: first [a/b:]
    all [
        set-path? :a-value
        error? try [eval :a-value] ;-- no value to assign after it...
    ]
)
(
    a-value: "1"
    1 == do :a-value
)
(void? do "")
(1 = do "1")
(3 = do "1 2 3")
(
    a-value: make tag! ""
    same? :a-value eval :a-value
)
(0:00 == eval 0:00)
(0.0.0 == eval 0.0.0)
(
    a-value: 'b-value
    b-value: 1
    1 == eval :a-value
)
; RETURN stops the evaluation
(
    f1: does [do [return 1 2] 2]
    1 = f1
)
; THROW stops evaluation
(
    1 = catch [
        do [
            throw 1
            2
        ]
        2
    ]
)
; BREAK stops evaluation
(
    blank? loop 1 [
        do [
            break
            2
        ]
        2
    ]
)
; do/next block tests
(
    success: false
    do/next [success: true success: false] 'b
    success
)
(
    all [
        1 = do/next [1 2] 'b
        [2] = b
    ]
)
(void? do/next [] 'b)
(error? do/next [try [1 / 0]] 'b)
(
    f1: does [do/next [return 1 2] 'b 2]
    1 = f1
)
; recursive behaviour
(1 = do [do [1]])
(1 = do "do [1]")
(1 == 1)
(3 = eval :eval :add 1 2)
; infinite recursion for block
(
    blk: [do blk]
    error? try blk
)
; infinite recursion for string
[#1896 (
    str: "do str"
    error? try [do str]
)]
; infinite recursion for do/next
(
    blk: [do/next blk 'b]
    error? try blk
)
(
    val1: try [do [1 / 0]]
    val2: try [do/next [1 / 0] 'b]
    val1/near = val2/near
)
