; functions/string/dehex.r

; DEHEX no longer tolerates non %xx or %XX patterns with % in source data
;
(error? trap ["a%b" = dehex "a%b"])
(error? trap ["a%~b" = dehex "a%~b"])

; !!! Strings don't tolerate NUL, so should you be able to DEHEX a BINARY!
; and get something like "a%00b" from it?  That would not enforce the rest
; of the BINARY! being UTF-8 and seems like it could be a bad idea.
(
    e: trap [dehex "a%00b"]
    e.id = 'illegal-zero-byte
)

("a b" = dehex "a%20b")
("a%b" = dehex "a%25b")
("a+b" = dehex "a%2bb")
("a+b" = dehex "a%2Bb")
("abc" = dehex "a%62c")

; #1986
("aβc" == dehex "a%ce%b2c")
((to-text #{61CEB263}) = dehex "a%CE%b2c")
(#{61CEB263} = to-binary dehex "a%CE%B2c")
("++" == dehex "%2b%2b")

; Per RFC 3896 2.1, all percent encodings should normalize to uppercase
;
("a%CE%B2c" == enhex "aβc")

[
    https://github.com/metaeducation/ren-c/issues/1003
    ("%C2%80" == enhex to-text #{C280})
]

; For what must be encoded, see https://stackoverflow.com/a/7109208/
(
    no-encode: unspaced [
        "ABCDEFGHIJKLKMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"
        "-._~:/?#[]@!$&'()*+,;="
    ]
    did all [
        no-encode == enhex no-encode
        no-encode == dehex no-encode
    ]
)

[#1986
    ("/form?v=ř" = dehex as text! #{2F666F726D3F763D254335253939})
]
