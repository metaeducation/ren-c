; %parse-text.test.reb
;
; A TEXT! rule will capture the actual match in a block.  But for a string, it
; will capture the *rule*.

; No-op rules need thought in UPARSE, in terms of NULL/BLANK! behavior.  But
; empty string should be a no-op on string input, and an empty rule should
; always match.
;
("" = uparse "" [""])

[(
    rule: [x: "a"]
    did all [
        uparse? "a" rule
        same? x second rule
    ]
)(
    data: ["a"]
    rule: [x: "a"]
    did all [
        uparse? data rule
        same? x first data
    ]
)]
