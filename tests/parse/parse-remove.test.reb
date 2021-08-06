; %parse-remove.test.reb
;
; Mutating operations in UPARSE raise some large questions; they were removed
; from Topaz entirely.  For the moment they are being considered.

(did all [
    uparse? text: "a ^/ " [
        while [newline remove [to <end>] | "a" [remove [to newline]] | skip]
    ]
    text = "a^/"
])
