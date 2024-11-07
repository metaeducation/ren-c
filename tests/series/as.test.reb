(
    block: copy [a b c]
    path: to path! block
    append block 'd
    path = 'a/b/c  ; AS was not legal
)
(
    block: copy [a b c]
    group: as group! block
    append block 'd
    group = the (a b c d)
)

; With UTF-8 Everywhere, AS will be able to alias series data for
; TEXT! to BLOB! to WORD!.  As a stopgap measure, series are
; copied but either the original is locked or freed, to help
; avoid a situation where the user modified one end of the AS
; expectating the other to change too.
(
    bin: as blob! copy "abc"
    all [
       #{616263} = bin
       #{61626364} = append bin #"d"
    ]
)
(
    bin: as blob! copy <abc>
    all [
        #{616263} = bin
        #{61626364} = append bin #"d"
    ]
)
(
    bin: copy as blob! 'abc
    all [
        #{616263} = bin
        #{61626364} = append bin #"d"
    ]
)
(
    bin: copy as blob! #abc
    all [
        #{616263} = bin
        #{61626364} = append bin #"d"
    ]
)

(
   bin: copy #{616263}
   txt: as text! bin
   all [
      "abc" = txt
      "abcd" = append txt #"d"
      #{61626364} = bin
   ]
)


~series-frozen~ !! (append as block! _ [a b c])
~series-frozen~ !! (append as text! _ "a b c")
~series-frozen~ !! (append as binary! _ #{AABBCC})

([[a b c]] = append to block! _ [a b c])
("a b c" = append to text! _ "a b c")
(#{AABBCC} = append to blob! _ #{AABBCC})

(_ = to blank! '{})
(_ = to blank! make tag! 0)
(_ = to blank! #{})
(_ = to blank! #"")

~???~ !! (to blank! '{a})
~???~ !! (to blank! <a>)
~???~ !! (to blank! #{AA})
~???~ !! (to blank! #"a")
