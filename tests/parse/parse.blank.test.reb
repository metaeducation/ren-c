; %parse-blank.test.reb
;
; BLANK! matches blanks in lists literally.  This is helpful in particular
; with breaking down paths with empty slots:
;
;    >> refinement-rule: [subparse path! [_ word!]]
;
;    >> parse [/a] [refinement-rule]
;    == a
;
; For strings and binaries, it is a synonym for SPACE.
;
; (At one time it was thought it might be how to say "match any value", as
; underscore is sometimes used in this wildcarding fashion in some languages:
;
;    >> parse [x <y> "z"] [_ _ _]
;    == "z"  ; one idea for the behavior of blank...
;
; ...but the @ combinator does this better.  See %parse-the.test.reb
;

(
    run-word-rule: [subparse path! [_ word!]]
   'a = parse [/a] [run-word-rule]
)

~parse-mismatch~ !! (parse [x] ['x blank])
('_ = parse [x _] ['x _])
('x = parse [x] [opt blank 'x <end>])

~parse-mismatch~ !! (parse [] [blank blank blank])

~parse-mismatch~ !! (parse [x <y> "z"] ['_ '_ '_])
(_ = parse [_ _ _] ['_ '_ '_])
(_ = parse [_ _ _] [_ _ _])

[
    ~parse-mismatch~ !! (parse "" [_])
    (space = parse " " [_])
    ~parse-mismatch~ !! (parse "" [blank])
    (space = parse " " [blank])
]

; !!! Should matching in a binary against blank return 32 or SPACE?
[
    ~parse-mismatch~ !! (parse #{} [_])
    (space = parse #{20} [_])
    ~parse-mismatch~ !! (parse #{} [blank])
    (space = parse #{20} [blank])
]
