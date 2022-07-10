; %parse-blank.test.reb
;
; BLANK! is a no-op which returns NULL, providing a small contrast from the
; no-op accomplished by an empty BLOCK!.  This fits into general ideas about
; blank's usage to "opt out" with the "blank-in-null-out" philosophy.
;
; (At one time it was thought it might be how to say "match any value", as
; underscore is sometimes used in this wildcarding fashion in some languages:
;
;    >> did parse [x <y> "z"] [_ _ _]
;    == #[true]
;
; ...but the <any> tag combinator serves this purpose more literately.)
;

('~null~ == meta parse [x] ['x blank])
([] == parse [x] [blank 'x <end>])

('~null~ == meta parse [] [blank blank blank])

(didn't parse [x <y> "z"] ['_ '_ '_])
('~blank~ == meta parse [_ _ _] ['_ '_ '_])
(
    q-blank: quote _
    '~blank~ == meta parse [_ _ _] [q-blank q-blank q-blank]
)

('~null~ == meta parse [] [[[blank blank blank]]])

[
    ('~null~ == meta parse [] [blank])
    ('~null~ == meta parse [a] [<any> blank])
    ('~null~ == meta parse [a] [blank <any> blank])
    ('~null~ == meta parse [a] ['a blank])
    ('~null~ == meta parse [a] [blank 'a blank])
    (
        wa: ['a]
        '~null~ == meta parse [a] [wa blank]
    )
    (
        wa: ['a]
        '~null~ == meta parse [a] [blank wa blank]
    )
    ('a == parse [a] [['b | blank] 'a])
    ('a == parse [a] [['b | [blank]] 'a])
    ('a == parse [a] [[['b | [blank]]] 'a])
]

[
    ('~null~ == meta parse "" [blank])
    ('~null~ == meta parse "a" [<any> blank])
    ('~null~ == meta parse "a" [blank <any> blank])
    ('~null~ == meta parse "a" [#a blank])
    ('~null~ == meta parse "a" [blank #a blank])
    (
        wa: [#a]
        '~null~ == meta parse "a" [wa blank]
    )
    (
        wa: [#a]
        '~null~ == meta parse "a" [blank wa blank]
    )
    (#a == parse "a" [[#b | blank] #a])
    (#a == parse "a" [[#b | [blank]] #a])
    (#a == parse "a" [[[#b | [blank]]] #a])
]

[
    ('~null~ == meta parse #{} [blank])
    ('~null~ == meta parse #{0A} [<any> blank])
    ('~null~ == meta parse #{0A} [blank <any> blank])
    ('~null~ == meta parse #{0A} [#{0A} blank])
    ('~null~ == meta parse #{0A} [blank #{0A} blank])
    (
        wa: [#{0A}]
        '~null~ == meta parse #{0A} [wa blank]
    )
    (
        wa: [#{0A}]
        '~null~ == meta parse #{0A} [blank wa blank]
    )
    (#{0A} == parse #{0A} [[#{0B} | blank] #{0A}])
    (#{0A} == parse #{0A} [[#{0B} | [blank]] #{0A}])
    (#{0A} == parse #{0A} [[[#{0B} | [blank]]] #{0A}])
]
