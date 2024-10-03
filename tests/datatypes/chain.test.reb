; %chain.test.reb
;
; The CHAIN! is a new datatype, peer to PATH! and TUPLE!, which
; uses colons as an interstitial delimiter.  One of its key
; applications is to refinements in functions, allow PATH! to
; specifically locate single instance invocations.

(all [
    let p: match path! 'a:b.c/:d.e
    p.1 = match chain! 'a:b.c
    p.2 = match chain! ':d.e
    p.1.1 = match word! 'a
    p.1.2 = match tuple! 'b.c
    p.1.2.1 = match word! 'b
    p.2.1 = match blank! _
])
