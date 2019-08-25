(
    block: copy [a b c]
    path: to path! block
    append block [d]
    path = 'a/b/c  ; AS was not legal
)
(
    block: copy [a b c]
    group: as group! block
    append block [d]
    group = lit (a b c d)
)

; With UTF-8 Everywhere, AS will be able to alias series data for
; TEXT! to BINARY! to WORD!.  As a stopgap measure, series are
; copied but either the original is locked or freed, to help
; avoid a situation where the user modified one end of the AS
; expectating the other to change too.
(
   bin: as binary! copy {abc}
   did all [
      #{616263} = bin
      #{61626364} = append bin #"d"
   ]
)
(
   bin: as binary! copy <abc>
   did all [
      #{616263} = bin
      #{61626364} = append bin #"d"
   ]
)
(
   bin: copy as binary! 'abc
   did all [
      #{616263} = bin
      #{61626364} = append bin #"d"
   ]
)
(
   bin: copy as binary! #abc
   did all [
      #{616263} = bin
      #{61626364} = append bin #"d"
   ]
)

(
   bin: copy #{616263}
   txt: as text! bin
   did all [
      "abc" = txt
      "abcd" = append txt #"d"
      #{61626364} = bin
   ]
)
