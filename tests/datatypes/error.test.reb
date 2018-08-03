; datatypes/error.r
(error? trap [1 / 0])
(not error? 1)
(error! = type of trap [1 / 0])

; error evaluation
(error? do head of insert copy [] trap [1 / 0])

; error that does not exist in the SCRIPT category--all of whose ids are
; reserved by the system and must be formed from mezzanine/user code in
; accordance with the structure the system would form.  Hence, illegal.
;
(trap/with [make error! [type: 'script id: 'nonexistent-id]] [true])

; triggered errors should not be assignable
;
(a: 1 error? trap [a: 1 / 0] :a =? 1)
(a: 1 error? trap [set 'a 1 / 0] :a =? 1)
(a: 1 error? trap [set/opt 'a 1 / 0] :a =? 1)

[#2190
    (127 = catch/quit [attempt [catch/quit [1 / 0]] quit/with 127])
]

; error types that should be predefined

(error? make error! [type: 'syntax id: 'scan-invalid])
(error? make error! [type: 'syntax id: 'scan-missing])
(error? make error! [type: 'syntax id: 'scan-extra])
(error? make error! [type: 'syntax id: 'scan-mismatch])
(error? make error! [type: 'syntax id: 'no-header])
(error? make error! [type: 'syntax id: 'bad-header])
(error? make error! [type: 'syntax id: 'malconstruct])
(error? make error! [type: 'syntax id: 'bad-char])
(error? make error! [type: 'syntax id: 'needs])

(error? make error! [type: 'script id: 'no-value])
(error? make error! [type: 'script id: 'need-value])
(error? make error! [type: 'script id: 'not-bound])
(error? make error! [type: 'script id: 'not-in-context])
(error? make error! [type: 'script id: 'no-arg])
(error? make error! [type: 'script id: 'expect-arg])
(error? make error! [type: 'script id: 'expect-val])
(error? make error! [type: 'script id: 'expect-type])
(error? make error! [type: 'script id: 'cannot-use])
(error? make error! [type: 'script id: 'invalid-arg])
(error? make error! [type: 'script id: 'invalid-type])
(error? make error! [type: 'script id: 'invalid-op])
(error? make error! [type: 'script id: 'no-op-arg])
(error? make error! [type: 'script id: 'invalid-data])
(error? make error! [type: 'script id: 'not-same-type])
(error? make error! [type: 'script id: 'not-related])
(error? make error! [type: 'script id: 'bad-func-def])
(error? make error! [type: 'script id: 'bad-func-arg])
(error? make error! [type: 'script id: 'no-refine])
(error? make error! [type: 'script id: 'bad-refines])
(error? make error! [type: 'script id: 'bad-refine])
(error? make error! [type: 'script id: 'bad-path-pick])
(error? make error! [type: 'script id: 'bad-path-poke])
(error? make error! [type: 'script id: 'bad-field-set])
(error? make error! [type: 'script id: 'dup-vars])
(error? make error! [type: 'script id: 'past-end])
(error? make error! [type: 'script id: 'missing-arg])
(error? make error! [type: 'script id: 'too-short])
(error? make error! [type: 'script id: 'too-long])
(error? make error! [type: 'script id: 'invalid-chars])
(error? make error! [type: 'script id: 'invalid-compare])
(error? make error! [type: 'script id: 'invalid-part])
(error? make error! [type: 'script id: 'no-return])
(error? make error! [type: 'script id: 'block-lines])
(error? make error! [type: 'script id: 'bad-bad])
(error? make error! [type: 'script id: 'bad-make-arg])
(error? make error! [type: 'script id: 'wrong-denom])
(error? make error! [type: 'script id: 'bad-compression])
(error? make error! [type: 'script id: 'dialect])
(error? make error! [type: 'script id: 'bad-command])
(error? make error! [type: 'script id: 'parse-rule])
(error? make error! [type: 'script id: 'parse-end])
(error? make error! [type: 'script id: 'parse-variable])
(error? make error! [type: 'script id: 'parse-command])
(error? make error! [type: 'script id: 'parse-series])

(error? make error! [type: 'math id: 'zero-divide])
(error? make error! [type: 'math id: 'overflow])
(error? make error! [type: 'math id: 'positive])
(error? make error! [type: 'math id: 'type-limit])
(error? make error! [type: 'math id: 'size-limit])
(error? make error! [type: 'math id: 'out-of-range])

(error? make error! [type: 'access id: 'protected-word])
(error? make error! [type: 'access id: 'hidden])
(error? make error! [type: 'access id: 'cannot-open])
(error? make error! [type: 'access id: 'not-open])
(error? make error! [type: 'access id: 'already-open])
(error? make error! [type: 'access id: 'no-connect])
(error? make error! [type: 'access id: 'not-connected])
(error? make error! [type: 'access id: 'no-script])
(error? make error! [type: 'access id: 'no-scheme-name])
(error? make error! [type: 'access id: 'no-scheme])
(error? make error! [type: 'access id: 'invalid-spec])
(error? make error! [type: 'access id: 'invalid-port])
(error? make error! [type: 'access id: 'invalid-actor])
(error? make error! [type: 'access id: 'invalid-port-arg])
(error? make error! [type: 'access id: 'no-port-action])
(error? make error! [type: 'access id: 'protocol])
(error? make error! [type: 'access id: 'invalid-check])
(error? make error! [type: 'access id: 'write-error])
(error? make error! [type: 'access id: 'read-error])
(error? make error! [type: 'access id: 'read-only])
(error? make error! [type: 'access id: 'timeout])
(error? make error! [type: 'access id: 'no-create])
(error? make error! [type: 'access id: 'no-delete])
(error? make error! [type: 'access id: 'no-rename])
(error? make error! [type: 'access id: 'bad-file-path])
(error? make error! [type: 'access id: 'bad-file-mode])
(error? make error! [type: 'access id: 'security])
(error? make error! [type: 'access id: 'security-level])
(error? make error! [type: 'access id: 'security-error])
(error? make error! [type: 'access id: 'no-codec])
(error? make error! [type: 'access id: 'bad-media])
(error? make error! [type: 'access id: 'no-extension])
(error? make error! [type: 'access id: 'bad-extension])
(error? make error! [type: 'access id: 'extension-init])

(error? make error! [type: 'user id: 'message])

(error? make error! [type: 'internal id: 'bad-path])
(error? make error! [type: 'internal id: 'not-here])
(error? make error! [type: 'internal id: 'no-memory])
(error? make error! [type: 'internal id: 'stack-overflow])
(error? make error! [type: 'internal id: 'globals-full])
(error? make error! [type: 'internal id: 'bad-sys-func])
(error? make error! [type: 'internal id: 'not-done])
(error? make error! [type: 'internal id: 'bad-utf8])

; are error reports for DO and EVALUATE consistent?
(
    val1: trap [do [1 / 0]]
    val2: trap [evaluate [1 / 0]]
    val1/near = val2/near
)

(
    e: trap [1 / 0]
    e/id = 'zero-divide
)

; #60, #1135
; This tests the NEAR positioning, though really only a few elements of
; the array are mirrored into the error.  This happens to go to the limit of
; 3, and shows that the infix expression start was known to the error.
(
    e1: trap [1 / 0]
    e2: trap [2 / 0]

    did all [
        e1/id = 'zero-divide
        e2/id = 'zero-divide
        [1 / 0] = copy/part e1/near 3
        [2 / 0] = copy/part e2/near 3
        e1 <> e2
    ]
)
