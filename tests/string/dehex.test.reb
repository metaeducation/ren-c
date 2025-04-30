; functions/string/dehex.r

; DEHEX no longer tolerates non %xx or %XX patterns with % in source data
;
~???~ !! (dehex "a%b")
~???~ !! (dehex "a%~b")

; !!! Strings don't tolerate embedded NUL, but the percent encoding spec
; allows %00.  You should be able to do DEHEX:BLOB and have this work, but
; the implementation needs revisiting to support that.
;
~illegal-zero-byte~ !! (dehex "a%00b")

(
    ; Dialect for validating reversible <-> and non-reversible -> encodings.
    ;
    ; 1. Accept lowercase, but canonize to uppercase, per RFC 3896 2.1
    ;
    ; 2. A case can be made for considering the encoding of characters that
    ;    don't need it to be an error by default.
    ;
    parse compose $() [
        "a%20b" <-> "a b"
        "a%25b" <-> "a%b"
        "a%ce%b2c" -> "aβc" -> "a%CE%B2c"  [1]
        "%2b%2b" -> "++" -> "++"  [2]
        "a%2Bb" -> "a+b" -> "a+b"  [2]
        "a%62c" -> "abc" -> "abc"  [2]
        "a%CE%B2c" <-> "aβc"
        (as text! #{2F666F726D3F763D254335253939}) -> "/form?v=ř"  #1986
    ][ some [
        let encoded: text!
        let arrow: ['<-> | '->]
        let decoded: text!
        let re-encoded: [when (arrow = '->) ['-> text!] | (encoded)]
        optional block!  ; headnote comment
        optional issue!  ; GitHub issue number
        (
            let de: dehex encoded
            if de != decoded [
                fail ["Decode of" @encoded "gave" @de "expected" @decoded]
            ]
            let en: enhex decoded
            if en != re-encoded [
                fail ["Encode of" @decoded "gave" @en "expected" @re-encoded]
            ]
        )
    ]]
    ok
)

; #1986
((to-text #{61CEB263}) = dehex "a%CE%b2c")
(#{61CEB263} = to-blob dehex "a%CE%B2c")

[
    https://github.com/metaeducation/ren-c/issues/1003
    ("%C2%80" = enhex to-text #{C280})
]

; For what must be encoded, see https://stackoverflow.com/a/7109208/
(
    no-encode: unspaced [
        "ABCDEFGHIJKLKMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"
        "-._~:/?#[]@!$&'()*+,;="
    ]
    all [
        no-encode = enhex no-encode
        no-encode = dehex no-encode
    ]
)
