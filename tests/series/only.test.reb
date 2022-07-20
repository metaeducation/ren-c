; %only.test.reb
;
; Was designed to create single element blocks to eliminate APPEND/ONLY.
; Thoughts to optimize it at the implementation level by making it immutable.
; These ideas were replaced by efficient generic quoting and using block
; isotopes for splice signaling.
;
; Currently moved to deprecated ONLY* for single element blocks that are
; not immutable--other names might be better (ENVELOP) though generic quoting
; is a better tool for efficiently putting things in a cheap "container"

([] = only* null)
([1] = only* 1)
([[1]] = only* only* 1)

(
    block: [x]
    j: only* block
    append first j <legal>
    [[x <legal>]] = j
)
