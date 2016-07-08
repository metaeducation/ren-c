; datatypes/symbol.r
[tag? <tag>]
[not tag? 1]
[tag! = type-of <tag>]
; minimum
[tag? #[tag! ""]]
[strict-equal? #[tag! ""] make tag! 0]
[strict-equal? #[tag! ""] to tag! ""]
["<tag>" == mold <tag>]
; bug#2169
["<ēee>" == mold <ēee>]
