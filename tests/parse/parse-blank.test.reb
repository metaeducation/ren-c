; %parse-blank.test.reb
;
; There was some debate over whether a literal blank as a PARSE rule might
; mean "match any value".  e.g.:
;
;    >> uparse? [x <y> "z"] [_ _ _]
;    == #[true]
;
; This idea runs up against the notion of BLANK! as a rule meaning "opt out"
; and act the same as an empty block.
;
;    >> uparse? [] [blank blank blank]
;    == #[true]
;
; Philosophically there has been some question of whether a literal thing
; must act the same as a fetched thing.  This is not true for active types
; like WORD!...so BLANK! might be able to be lax in this respect:
;
; https://forum.rebol.info/t/1348
;
; However, the <any> tag works well for meaning "match anything here" and is
; clearer.  So that has become the new favorite.  The question remains open,
; and for the moment literal blanks have no behavior.

(uparse? [x] ['x blank])
(uparse? [x] [blank 'x <end>])

(uparse? [] [blank blank blank])

(not uparse? [x <y> "z"] ['_ '_ '_])
(uparse? [_ _ _] ['_ '_ '_])
(
    q-blank: quote _
    uparse? [_ _ _] [q-blank q-blank q-blank]
)

(uparse? [] [[[blank blank blank]]])

[
    (uparse? [] [blank])
    (uparse? [a] [<any> blank])
    (uparse? [a] [blank <any> blank])
    (uparse? [a] ['a blank])
    (uparse? [a] [blank 'a blank])
    (
        wa: ['a]
        uparse? [a] [wa blank]
    )
    (
        wa: ['a]
        uparse? [a] [blank wa blank]
    )
    (uparse? [a] [['b | blank] 'a])
    (uparse? [a] [['b | [blank]] 'a])
    (uparse? [a] [[['b | [blank]]] 'a])
]

[
    (uparse? "" [blank])
    (uparse? "a" [<any> blank])
    (uparse? "a" [blank <any> blank])
    (uparse? "a" [#a blank])
    (uparse? "a" [blank #a blank])
    (
        wa: [#a]
        uparse? "a" [wa blank]
    )
    (
        wa: [#a]
        uparse? "a" [blank wa blank]
    )
    (uparse? "a" [[#b | blank] #a])
    (uparse? "a" [[#b | [blank]] #a])
    (uparse? "a" [[[#b | [blank]]] #a])
]

[
    (uparse? #{} [blank])
    (uparse? #{0A} [<any> blank])
    (uparse? #{0A} [blank <any> blank])
    (uparse? #{0A} [#{0A} blank])
    (uparse? #{0A} [blank #{0A} blank])
    (
        wa: [#{0A}]
        uparse? #{0A} [wa blank]
    )
    (
        wa: [#{0A}]
        uparse? #{0A} [blank wa blank]
    )
    (uparse? #{0A} [[#{0B} | blank] #{0A}])
    (uparse? #{0A} [[#{0B} | [blank]] #{0A}])
    (uparse? #{0A} [[[#{0B} | [blank]]] #{0A}])
]
