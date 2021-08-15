; %parse-any.test.reb
;
; ANY in UPARSE is not an iterative construct, but a way of treating a block as
; a list of alternative rules.  This can be more convenient than having to make
; a rule block that has the alternatives separated by `|`, which has difficult
; issues to resolve on the edges.
;
; It's necessary to put the BLOCK! in a group, because a plain BLOCK! already
; has semantics in UPARSE.  For any to receive it literally and do its own form
; of processing, it has to be a rule product.

("b" = uparse "ab" [some any (["a" "b"])])
(null = uparse "abc" [some any (["a" "b"])])

(3 = uparse ["foo" <baz> 3] [some any ([tag! integer! text!])])

; Alternative notation: use a THE-BLOCK!

("b" = uparse "ab" [some any @["a" "b"]])
(null = uparse "abc" [some any @["a" "b"]])

(3 = uparse ["foo" <baz> 3] [some any @[tag! integer! text!]])
