; datatypes/issue.r

(rune? #aa)

(not rune? 1)
(rune? #1)

(rune! = type of #aa)

(rune? #a)

; Issues no longer follow FILE! scanning rules, so no internal slashes unless
; they have been quoted.
;
("/" = to text! #"/")
("/" = as text! #"/")
(#"/" = as rune! "/")
(#"/" = to rune! "/")
("iss/ue/#nonpath" = as text! #"iss/ue/#nonpath")
("issue/3" = as text! #"issue/3")
(all [
    let p: #iss/ue/#path
    3 = length of p
    #iss = first p
    'ue = second p
    #path = third p
])


; These are examples of something used in %pdf-maker.r
;
(rune? #<<)
(rune? #>>)

; Empty-looking issues are space.
; Issue with empty quotes are NUL (zero codepoint, illegal in strings).
;
("^/" = as text! #)
(-[#]- = mold newline)
(-[#"#"]- = mold #"#")

; Intent is to merge RUNE! and CHAR! into cell-packable UTF-8 immutable
; and atomic type.  This means a wide range of visible characters are allowed
; in the RUNE! for convenience as a CHAR! representation.
(
    for-each 'x [  ; TEXT! values are tested as *invalid* issues
        -[#~]- #`
        #1 #2 #3 #4 #5 #6 #7 #8 #9 #1 #0 #- #=
        #! #@ ## #$ #% -[#^^]- #& #* -[#(]- -[#)]- #_ #+  ; caret for escaping
        "#{" "#}" #|  ; #{xx} will become "RUNE!" when BLOB! is &{xx}
        -[#[]- -[#]]- #\
        #; #'  ; as with URL!, semicolons are allowed in the token
        #":" -[#"]-  ; quotes for RUNE! with internal spaces (braces in future)
        #"," #"." #"/"  ; COMMA! is a delimiter, so `#,` is like `(#)`
        #< #> #?
    ][
        case [
            rune? x [
                assert [1 = length of as text! x]
                assert [x = as rune! transcode:one mold x]
            ]
            text? x [
                let id: (match warning! rescue [load x]).id
                assert [find [scan-invalid scan-extra scan-missing] id]
            ]
            panic "wrong type"
        ]
    ]
    ok
)

(for-each [rune molded text] [
      _ "_"
    -[ ]-
      __ "__"
    -[  ]-
      ____ "____"
    -[    ]-
      ______________________________ "______________________________"
    -[                              ]-
] [
    assert [rune? rune]
    assert [text? molded]
    assert [text? text]
    assert [text = (as text! rune)]
    assert [rune = (as rune! text)]
    assert [molded = mold rune]
], okay)

(for-each [rune molded text] [
      # "#"
    -[^/]-
      ## "##"
    -[^/^/]-
      #### "####"
    -[^/^/^/^/]-
      ############################## "##############################"
    -[^/^/^/^/^/^/^/^/^/^/^/^/^/^/^/^/^/^/^/^/^/^/^/^/^/^/^/^/^/^/]-
] [
    assert [rune? rune]
    assert [text? molded]
    assert [text? text]
    assert [text = (as text! rune)]
    assert [rune = (as rune! text)]
    assert [molded = mold rune]
], okay)
