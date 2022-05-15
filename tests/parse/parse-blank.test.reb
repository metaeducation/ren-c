; %parse-blank.test.reb
;
; BLANK! is a no-op which returns NULL, providing a small contrast from the
; no-op accomplished by an empty BLOCK!.  This fits into general ideas about
; blank's usage to "opt out" with the "blank-in-null-out" philosophy.
;
; (At one time it was thought it might be how to say "match any value", as
; underscore is sometimes used in this wildcarding fashion in some languages:
;
;    >> did uparse [x <y> "z"] [_ _ _]
;    == #[true]
;
; ...but the <any> tag combinator serves this purpose more literately.)
;

('~null~ == meta uparse [x] ['x blank])
([] == uparse [x] [blank 'x <end>])

('~null~ == meta uparse [] [blank blank blank])

(didn't uparse [x <y> "z"] ['_ '_ '_])
('~blank~ == meta uparse [_ _ _] ['_ '_ '_])
(
    q-blank: quote _
    '~blank~ == meta uparse [_ _ _] [q-blank q-blank q-blank]
)

('~null~ == meta uparse [] [[[blank blank blank]]])

[
    ('~null~ == meta uparse [] [blank])
    ('~null~ == meta uparse [a] [<any> blank])
    ('~null~ == meta uparse [a] [blank <any> blank])
    ('~null~ == meta uparse [a] ['a blank])
    ('~null~ == meta uparse [a] [blank 'a blank])
    (
        wa: ['a]
        '~null~ == meta uparse [a] [wa blank]
    )
    (
        wa: ['a]
        '~null~ == meta uparse [a] [blank wa blank]
    )
    ('a == uparse [a] [['b | blank] 'a])
    ('a == uparse [a] [['b | [blank]] 'a])
    ('a == uparse [a] [[['b | [blank]]] 'a])
]

[
    ('~null~ == meta uparse "" [blank])
    ('~null~ == meta uparse "a" [<any> blank])
    ('~null~ == meta uparse "a" [blank <any> blank])
    ('~null~ == meta uparse "a" [#a blank])
    ('~null~ == meta uparse "a" [blank #a blank])
    (
        wa: [#a]
        '~null~ == meta uparse "a" [wa blank]
    )
    (
        wa: [#a]
        '~null~ == meta uparse "a" [blank wa blank]
    )
    (#a == uparse "a" [[#b | blank] #a])
    (#a == uparse "a" [[#b | [blank]] #a])
    (#a == uparse "a" [[[#b | [blank]]] #a])
]

[
    ('~null~ == meta uparse #{} [blank])
    ('~null~ == meta uparse #{0A} [<any> blank])
    ('~null~ == meta uparse #{0A} [blank <any> blank])
    ('~null~ == meta uparse #{0A} [#{0A} blank])
    ('~null~ == meta uparse #{0A} [blank #{0A} blank])
    (
        wa: [#{0A}]
        '~null~ == meta uparse #{0A} [wa blank]
    )
    (
        wa: [#{0A}]
        '~null~ == meta uparse #{0A} [blank wa blank]
    )
    (#{0A} == uparse #{0A} [[#{0B} | blank] #{0A}])
    (#{0A} == uparse #{0A} [[#{0B} | [blank]] #{0A}])
    (#{0A} == uparse #{0A} [[[#{0B} | [blank]]] #{0A}])
]
