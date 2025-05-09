; datatypes/error.r
(warning? trap [1 / 0])
(not warning? 1)
(warning! = type of trap [1 / 0])

; error evaluation
(warning? eval head of insert copy [] trap [1 / 0])

; error that does not exist in the SCRIPT category--all of whose ids are
; reserved by the system and must be formed from mezzanine/user code in
; accordance with the structure the system would form.  Hence, illegal.
;
(trap [make warning! [type: 'script id: 'nonexistent-id]] then [okay])

; triggered errors should not be assignable
;
(a: 1 warning? trap [a: 1 / 0] a = 1)
(a: 1 warning? trap [set $a 1 / 0] a = 1)
(a: 1 warning? trap [set:any $a 1 / 0] a = 1)

[#2190
    ~zero-divide~ !! (
        catch [1 / 0]
    )
]

; error types that should be predefined

(warning? make warning! [type: 'syntax id: 'scan-invalid])
(warning? make warning! [type: 'syntax id: 'scan-missing])
(warning? make warning! [type: 'syntax id: 'scan-extra])
(warning? make warning! [type: 'syntax id: 'scan-mismatch])
(warning? make warning! [type: 'syntax id: 'no-header])
(warning? make warning! [type: 'syntax id: 'bad-header])
(warning? make warning! [type: 'syntax id: 'malconstruct])
(warning? make warning! [type: 'syntax id: 'bad-char])
(warning? make warning! [type: 'syntax id: 'needs])

(warning? make warning! [type: 'script id: 'no-value])
(warning? make warning! [type: 'script id: 'bad-word-get])
(warning? make warning! [type: 'script id: 'not-bound])
(warning? make warning! [type: 'script id: 'not-in-context])
(warning? make warning! [type: 'script id: 'no-arg])
(warning? make warning! [type: 'script id: 'expect-arg])
(warning? make warning! [type: 'script id: 'expect-val])
(warning? make warning! [type: 'script id: 'expect-type])
(warning? make warning! [type: 'script id: 'cannot-use])
(warning? make warning! [type: 'script id: 'invalid-arg])
(warning? make warning! [type: 'script id: 'invalid-type])
(warning? make warning! [type: 'script id: 'invalid-op])
(warning? make warning! [type: 'script id: 'no-op-arg])
(warning? make warning! [type: 'script id: 'invalid-data])
(warning? make warning! [type: 'script id: 'not-same-type])
(warning? make warning! [type: 'script id: 'not-related])
(warning? make warning! [type: 'script id: 'bad-func-def])
(warning? make warning! [type: 'script id: 'bad-func-arg])
(warning? make warning! [type: 'script id: 'no-refine])
(warning? make warning! [type: 'script id: 'bad-refines])
(warning? make warning! [type: 'script id: 'bad-parameter])
(warning? make warning! [type: 'script id: 'bad-pick])
(warning? make warning! [type: 'script id: 'bad-poke])
(warning? make warning! [type: 'script id: 'bad-field-set])
(warning? make warning! [type: 'script id: 'dup-vars])
(warning? make warning! [type: 'script id: 'index-out-of-range])
(warning? make warning! [type: 'script id: 'missing-arg])
(warning? make warning! [type: 'script id: 'too-short])
(warning? make warning! [type: 'script id: 'too-long])
(warning? make warning! [type: 'script id: 'invalid-chars])
(warning? make warning! [type: 'script id: 'invalid-compare])
(warning? make warning! [type: 'script id: 'invalid-part])
(warning? make warning! [type: 'script id: 'no-return])
(warning? make warning! [type: 'script id: 'bad-bad])
(warning? make warning! [type: 'script id: 'bad-make-arg])
(warning? make warning! [type: 'script id: 'wrong-denom])
(warning? make warning! [type: 'script id: 'bad-compression])
(warning? make warning! [type: 'script id: 'dialect])
(warning? make warning! [type: 'script id: 'bad-command])
(warning? make warning! [type: 'script id: 'parse3-rule])
(warning? make warning! [type: 'script id: 'parse3-end])
(warning? make warning! [type: 'script id: 'parse3-variable])
(warning? make warning! [type: 'script id: 'parse3-command])
(warning? make warning! [type: 'script id: 'parse3-series])
(warning? make warning! [type: 'script id: 'bad-utf8])
(warning? make warning! [type: 'script id: 'done-enumerating])

(warning? make warning! [type: 'math id: 'zero-divide])
(warning? make warning! [type: 'math id: 'overflow])
(warning? make warning! [type: 'math id: 'positive])
(warning? make warning! [type: 'math id: 'type-limit])
(warning? make warning! [type: 'math id: 'size-limit])
(warning? make warning! [type: 'math id: 'out-of-range])

(warning? make warning! [type: 'access id: 'protected-word])
(warning? make warning! [type: 'access id: 'hidden])
(warning? make warning! [type: 'access id: 'cannot-open])
(warning? make warning! [type: 'access id: 'not-open])
(warning? make warning! [type: 'access id: 'already-open])
(warning? make warning! [type: 'access id: 'no-connect])
(warning? make warning! [type: 'access id: 'not-connected])
(warning? make warning! [type: 'access id: 'no-script])
(warning? make warning! [type: 'access id: 'no-scheme-name])
(warning? make warning! [type: 'access id: 'no-scheme])
(warning? make warning! [type: 'access id: 'invalid-spec])
(warning? make warning! [type: 'access id: 'invalid-port])
(warning? make warning! [type: 'access id: 'invalid-actor])
(warning? make warning! [type: 'access id: 'invalid-port-arg])
(warning? make warning! [type: 'access id: 'no-port-action])
(warning? make warning! [type: 'access id: 'protocol])
(warning? make warning! [type: 'access id: 'invalid-check])
(warning? make warning! [type: 'access id: 'write-error])
(warning? make warning! [type: 'access id: 'read-error])
(warning? make warning! [type: 'access id: 'read-only])
(warning? make warning! [type: 'access id: 'timeout])
(warning? make warning! [type: 'access id: 'no-create])
(warning? make warning! [type: 'access id: 'no-delete])
(warning? make warning! [type: 'access id: 'no-rename])
(warning? make warning! [type: 'access id: 'bad-file-path])
(warning? make warning! [type: 'access id: 'bad-file-mode])
(warning? make warning! [type: 'access id: 'security])
(warning? make warning! [type: 'access id: 'security-level])
(warning? make warning! [type: 'access id: 'security-error])
(warning? make warning! [type: 'access id: 'no-codec])
(warning? make warning! [type: 'access id: 'bad-media])
(warning? make warning! [type: 'access id: 'no-extension])
(warning? make warning! [type: 'access id: 'bad-extension])
(warning? make warning! [type: 'access id: 'extension-init])

(warning? make warning! [type: 'user id: 'message])

(warning? make warning! [type: 'internal id: 'bad-path])
(warning? make warning! [type: 'internal id: 'not-here])
(warning? make warning! [type: 'internal id: 'no-memory])
(warning? make warning! [type: 'internal id: 'stack-overflow])
(warning? make warning! [type: 'internal id: 'globals-full])
(warning? make warning! [type: 'internal id: 'bad-sys-func])

; are error reports for DO and EVALUATE consistent?
(
    e1: sys.util/rescue [do "Rebol [] 1 / 0"]
    e2: sys.util/rescue [evaluate [1 / 0]]
    e1.near = e2.near
)

~zero-divide~ !! (1 / 0)

; #60, #1135
; This tests the NEAR positioning, though really only a few elements of
; the list are mirrored into the error.  This happens to go to the limit of
; 3, and shows that the infix expression start was known to the error.
;
; !!! This used to use `/` instead of divide, but a period of time where
; `/` was a zero-length path complicated the error, review.
(
    e1: trap [divide 1 0]
    e2: trap [divide 2 0]

    all [
        e1.id = 'zero-divide
        e2.id = 'zero-divide
        [divide 1 0] = copy:part e1.near 3
        [divide 2 0] = copy:part e2.near 3
        e1 <> e2
    ]
)
