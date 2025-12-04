; %trash.test.r
;
; TRASH! is the antiform of RUNE!.  It's a sort of a "Redbol UNSET!
; which can hold a message", in the sense that plain WORD! access
; will trigger an error, so you have to use ^WORD! access to get it.
;
; (Trash not to be confused with a true unset state, which needs
; special functions to detect and can't be read with either WORD!
; or ^WORD!)
;
; Assigning trash via plain SET-WORD! is legal, as a way to "poison"
; a variable, and it has many applications related to that.
;
; https://rebol.metaeducation.com/t/trash-runes-in-the-wild/2278


(trash? ~)
(trash? anti _)
((lift ~) = (lift anti _))

~bad-word-get~ !! (
    labeled: ~#[this is a trash]~
    labeled
)

(
    labeled: ~#[this is a trash]~ 
    #"this is a trash" = unanti ^labeled
)
