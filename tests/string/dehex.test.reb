; functions/string/dehex.r

; DEHEX no longer tolerates non %xx or %XX patterns with % in source data
;
(error? trap ["a%b" = dehex "a%b"])
(error? trap ["a%~b" = dehex "a%~b"])

("a^@b" = dehex "a%00b")
("a b" = dehex "a%20b")
("a%b" = dehex "a%25b")
("a+b" = dehex "a%2bb")
("a+b" = dehex "a%2Bb")
("abc" = dehex "a%62c")

; #1986
("aβc" = dehex "a%ce%b2c")
((to-string #{61CEB263}) = dehex "a%CE%b2c")
(#{61CEB263} = to-binary dehex "a%CE%B2c")

; Per RFC 3896 2.1, all percent encodings should normalize to uppercase
;
("a%CE%B2c" = enhex "aβc")

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
