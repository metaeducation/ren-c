; datatypes/issue.r

(issue? #aa)

(not issue? 1)
(issue? #1)

(issue! = type of #aa)

(issue? #a)

; Issues follow the rules for FILE! scanning, so they allow internal slashes.
;
("/" = to text! #/)
("/" = as text! #/)
(#/ = as issue! "/")
(#/ = to issue! "/")
("iss/ue/nonpath" = as text! #iss/ue/nonpath)
("issue/3" = as text! #issue/3)

; These are examples of something used in %pdf-maker.r
;
(issue? #<<)
(issue? #>>)

; Empty-looking issues are the zero codepoint (zero codepoints are illegal
; in strings).
;
("" = as text! #)
("#" = mold as issue! "")
({#} = mold #)

; Intent is to merge ISSUE! and CHAR! into cell-packable UTF-8 immutable
; and atomic type.  This means a wide range of visible characters are allowed
; in the ISSUE! for convenience as a CHAR! representation.
(
    for-each x [  ; TEXT! values are tested as *invalid* issues
        #~ #`
        #1 #2 #3 #4 #5 #6 #7 #8 #9 #1 #0 #- #=
        #! #@ ## #$ #% {#^^} #& #* {#(} {#)} #_ #+  ; caret used for escaping
        "#{" "#}" #|  ; #{xx} will become "ISSUE!" when BINARY! is &{xx}
        {#[} {#]} #\
        #; #'  ; as with URL!, semicolons are allowed in the token
        #: {#"}  ; quotes for ISSUE! with internal spaces (braces in future)
        #"," #. #/  ; COMMA! is a delimiter, so `#,` is like `(#)`
        #< #> #?
    ][
        case [
            issue? x [
                assert [1 = length of as text! x]
                assert [x = as issue! load mold x]
            ]
            text? x [
                id: (match error! trap [load x])/id
                assert [find [scan-invalid scan-extra scan-missing] id]
            ]
            fail "wrong type"
        ]
    ]
    true
)
