; In historical Rebol, UNLESS was a synonym for IF NOT.  This somewhat
; unambitious use of the word has been replaced in Ren-C with an infix
; operator that is siilar to a non-short-circuit OR, but prefers the right
; hand side's result:
;
;     >> x: {default} unless case [1 > 2 [{nope}] 3 > 4 [{not this either}]]
;     >> print x
;     default
;
; Under its new design, it is likely this change is going to be kept:
;
; https://forum.rebol.info/t/hedging-on-unless-for-beta-one/881

(
    20 = (10 unless 20)
)(
    10 = (10 unless null)
)(
    10 = (10 unless null)
)(
    x: 10 + 20 unless case [
        null [<no>]
        null [<nope>]
        null [<nada>]
    ]
    x = 30
)(
    x: 10 + 20 unless case [
        null [<no>]
        okay [<yip!>]
        null [<nada>]
    ]
    x = <yip!>
)
