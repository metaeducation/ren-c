; %parse.splice.test.r
;
; SPLICE! matches literally across the content for lists, and the JOIN'ed
; representation is matched for TEXT! and BLOB! (future efficiencies may
; be such that the matching does not need to merge the parts into a string
; in order to conduct the find on those, but for the moment the internals
; do just JOIN the TEXT! or BLOB! and then do the FIND)
;
; It doesn't matter whether you use `rule` or `@rule`.  In particular this
; makes it useful to have an empty splice serve as something that can be
; a no-op in a way that an empty block rule can't...because `@rule` of a
; BLOCK! will match the block literally.

(
    rule: ~("a" b #c)~
    rule = parse ["a" b #c] [rule]
)(
    rule: ~("a" b #c)~
    rule = parse "abc" [rule]
)(
    rule: ~(#{AA} #{BB} #{CC})~
    rule = parse #{AABBCC} [rule]
)

(
    rule: ~("a" b #c)~
    rule = parse ["a" b #c] [@rule]
)(
    rule: ~("a" b #c)~
    rule = parse "abc" [@rule]
)(
    rule: ~(#{AA} #{BB} #{CC})~
    rule = parse #{AABBCC} [@rule]
)

(~()~ = parse [a] ['a none])
(~()~ = parse "a" ["a" none])
(~()~ = parse #{AA} [#{AA} none])

(~()~ = parse [a] ['a @none])
(~()~ = parse "a" ["a" @none])
(~()~ = parse #{AA} [#{AA} @none])
