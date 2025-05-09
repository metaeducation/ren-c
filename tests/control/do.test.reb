; functions/control/do.r
(
    success: null
    eval [success: okay]
    success
)
(1 = reeval :abs -1)
(
    a-value: to binary! "1 + 1"
    2 = do a-value
)
(
    a-value: charset ""
    same? a-value reeval a-value
)
; eval block start
(void? eval [])
(:abs = eval [:abs])
(
    a-value: #{}
    same? a-value eval reduce [a-value]
)
(
    a-value: charset ""
    same? a-value eval reduce [a-value]
)
(
    a-value: []
    same? a-value eval reduce [a-value]
)
(same? blank! eval reduce [blank!])
(1/Jan/0000 = eval [1/Jan/0000])
(0.0 = eval [0.0])
(1.0 = eval [1.0])
(
    a-value: me@here.com
    same? a-value eval reduce [a-value]
)
(error? eval [sys/util/rescue [1 / 0]])
(
    a-value: %""
    same? a-value eval reduce [a-value]
)
(
    a-value: does []
    same? :a-value eval [:a-value]
)
(
    a-value: first [:a-value]
    :a-value = eval reduce [:a-value]
)
(#"^@" = eval [#"^@"])
(0 = eval [0])
(1 = eval [1])
(#a = eval [#a])
(
    a-value: first ['a/b]
    :a-value = eval [:a-value]
)
(
    a-value: first ['a]
    :a-value = eval [:a-value]
)
(okay = eval [okay])
(null = eval [null])
($1 = eval [$1])
(same? :append eval [:append])
(blank? eval [_])
(
    a-value: make object! []
    same? :a-value eval reduce [:a-value]
)
(
    a-value: first [()]
    same? :a-value eval [:a-value]
)
(same? get '+ eval [get '+])
(0x0 = eval [0x0])
(
    a-value: 'a/b
    :a-value = eval [:a-value]
)
(
    a-value: make port! http://
    port? eval reduce [:a-value]
)
(/a = eval [/a])
(
    a-value: first [a/b:]
    :a-value = eval [:a-value]
)
(
    a-value: first [a:]
    :a-value = eval [:a-value]
)
(
    a-value: ""
    same? :a-value eval reduce [:a-value]
)
(
    a-value: make tag! ""
    same? :a-value eval reduce [:a-value]
)
(0:00 = eval [0:00])
(0.0.0 = eval [0.0.0])
(void? eval [()])
('a = eval ['a])
; eval block end
(
    a-value: blank!
    same? a-value reeval a-value
)
(1/Jan/0000 = reeval 1/Jan/0000)
(0.0 = reeval 0.0)
(1.0 = reeval 1.0)
(
    a-value: me@here.com
    same? a-value reeval a-value
)
(error? sys/util/rescue [do sys/util/rescue [1 / 0] 1])
(
    a-value: does [5]
    5 = reeval :a-value
)
(
    a: 12
    a-value: first [:a]
    :a = reeval :a-value
)
(#"^@" = reeval #"^@")
(0 = reeval 0)
(1 = reeval 1)
(#a = reeval #a)
;-- CC#2101, #1434
(
    a-value: first ['a/b]
    all [
        lit-path? a-value
        path? reeval :a-value
        (as path! :a-value) = (reeval :a-value)
    ]
)
(
    a-value: first ['a]
    all [
        lit-word? a-value
        word? reeval :a-value
        (to-word :a-value) = (reeval :a-value)
    ]
)
(okay = reeval meta okay)
(null = reeval meta null)
($1 = reeval $1)
(null? try reeval (specialize 'of [property: 'type]) null)
(null? eval void)
(
    a-value: make object! []
    same? :a-value reeval :a-value
)
(
    a-value: first [(2)]
    2 = eval as block! :a-value
)
(
    a-value: 'a/b
    a: make object! [b: 1]
    1 = reeval :a-value
)
(
    a-value: make port! http://
    port? reeval :a-value
)
(
    a-value: first [a/b:]
    all [
        set-path? :a-value
        error? sys/util/rescue [reeval :a-value] ;-- no value to assign after it...
    ]
)
(
    a-value: "1"
    1 = do :a-value
)
(void? do "")
(1 = do "1")
(3 = do "1 2 3")
(
    a-value: make tag! ""
    same? :a-value reeval :a-value
)
(0:00 = reeval 0:00)
(0.0.0 = reeval 0.0.0)
(
    a-value: 'b-value
    b-value: 1
    1 = reeval :a-value
)
; RETURN stops the evaluation
(
    f1: func [] [eval [return 1 2] 2]
    1 = f1
)
; THROW stops evaluation
(
    1 = catch [
        eval [
            throw 1
            2
        ]
        2
    ]
)
; BREAK stops evaluation
(
    null? repeat 1 [
        eval [
            break
            2
        ]
        2
    ]
)
; evaluate block tests
(
    success: null
    evaluate/step3 [success: okay success: null] 'dummy
    success
)
(
    b: evaluate/step3 [1 2] 'value
    all [
        1 = value
        [2] = b
    ]
)
(
    value: <untouched>
    all [
        null? evaluate/step3 [] 'value
        null? value
    ]
)
(
    evaluate/step3 [sys/util/rescue [1 / 0]] 'value
    error? value
)
(
    f1: func [] [evaluate [return 1 2] 2]
    1 = f1
)
; recursive behaviour
(1 = eval [eval [1]])
(1 = do "eval [1]")
(1 = 1)
(3 = reeval :reeval :add 1 2)
; infinite recursion for block
(
    blk: [eval blk]
    error? sys/util/rescue blk
)
; infinite recursion for string
[#1896 (
    str: "do str"
    error? sys/util/rescue [do str]
)]
; infinite recursion for evaluate
(
    blk: [b: evaluate/step3 blk 'dummy]
    error? sys/util/rescue blk
)
