Rebol [
    system: "Ren-C Language Interpreter and Run-time Environment"
    title: "Canonical Words Where Order or Optimization in Lib Lookup Matters"
    rights: --[
        Copyright 2012-2021 Ren-C Open Source Contributors
        Copyright 2012 REBOL Technologies
        REBOL is a trademark of REBOL Technologies
    ]--
    license: --[
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    ]--
    purpose: --[
        These are words that are used internally by Rebol, which and are
        canonized with small integer SYM_XXX constants.  These constants can
        be quickly used in switch() statements, and a lookup table is on
        hand to turn SYM_XXX integers into the corresponding symbol pointer.

        The reason ordered words are kept in a separate file is that the
        symbol space overlaps with the names of natives and generics that
        are put into lib.  Due to optimizations, this means words where the
        relative order matters need to establish their indexes in lib that
        the natives then must accomodate.

        Unordered words do not forcibly have slots in the optimized range of
        lib, so they are in a separate file.
    ]--
]

void
quasar
trash
null
okay
hole
NUL

atom?
element?
fundamental?
quasi?
bindable?

newline
space

sys
system
lib

delimit  ; built in %base-defs.r on JOIN TEXT!, but called by system
print  ; used by PRINT* hack

compose  ; core implementation is COMPOSE2, but some want the specialization

file
sigil

; When building PATH! and TUPLE! the system collapses 2-element sequences
; that are all SPACE to these words.
;
/  ; SYM_SLASH_1
.  ; SYM_DOT_1
:  ; SYM_COLON_1


; === EXTENSIBLE EVALUATION ===
;
; Functions called by the evaluator to interpret types.  Can be redefined
; off the cuff, e.g. local to a function, to change the behavior of fundamental
; types within a scope.

fence!-EVAL  ; e.g. a function that turns {x: 10, y: null} => OBJECT!


; === SEQUENTIAL PARSE3 SYMBOLS FOR SWITCH() ===
;
; An R3-Alpha optimization that we preserve (for now) is that the keywords
; being switch()'d on in PARSE are in a fixed symbol range.  This makes it
; a quick test to tell if something is a parse pre-match or match keyword.
;
; There's also value to having these "packed densely" for the switch statement
; itself, so the compiler can use a jump table.  Try to keep these in order
; and matching the PARSE3 code, for as long as it's relevant (which will
; likely be some time, as UPARSE is going to be too slow for day-to-day
; bootstrapping in the foreseeable future)
;
<MIN_SYM_PARSE3>  ; no </> means next symbol (SYM_SOME is MIN_SYM_PARSE3)
    some
    opt
    optional
    try
    repeat
    further  ; https://forum.rebol.info/t/1593
    let
    not  ; turned to _not_ for SYM__NOT_, see TO-C-NAME for why this is weird
    ahead  ; Ren-C addition (also in Red)
    remove
    insert
    change
    when
    accept
    break
    reject
    veto
    inline
    cond
    seek  ; Ren-C addition
    ;
    ; DEPRECATED PARSE PRE-MATCH WORDS (still have case statements for errors,
    ; but put these cases last in case it doesn't do a jump table)
    ;
    and  ; replaced by AHEAD
    while  ; new meaning in UPARSE, use OPT SOME for old meaning
    any  ; new meaning in UPARSE, use OPT SOME FURTHER for old meaning
    copy  ; use `x: across rule` for this intent
    set  ; deprecated in PARSE3 (should it be supported?)
    limit  ; was never implemented
    return  ; is ACCEPT now to not confuse with FUNC's RETURN
<MIN_SYM_PARSE3_MATCH>
    skip
    one
    to
    thru
    the
    into
    ;
    ; DEPRECATED PARSE MATCH WORDS
    ;
    quote  ; use THE instead
    end
</MAX_SYM_PARSE3>  ;  </> means prior symbol (SYM_END is MAX_SYM_PARSE3)


;
; PARSE KEYWORDS NOT PART OF switch() STATEMENT
;
across  ; SYM_ACROSS checked as part of SET-WORD!'s code
|  ; done when skipping across rules
||  ; would be done skipping across rules, but not implemented in PARSE3


; definitional forms as DEFINITIONAL-RETURN, DEFINITIONAL-BREAK, ...
#return
#break
yield
continue
stop
quit

; It is convenient to be able to say `for-each [_ x y] [1 2 3 ...] [...]` and
; let the space indicate you are not interested in a value.  This might be
; doable with a generalized "anonymous key" system.  But for now it is assumed
; that all keys have unique symbols.
;
; To get the feature for today, we use some dummy symbols.  They cannot be
; used alongside the actual names `dummy1`, `dummy2`, etc...but rather
; than pick a more esoteric name then `map-each [_ dummy1] ...` is just an
; error.  Using simple names makes the frame more legible.
;
dummy1
dummy2
dummy3
dummy4
dummy5
dummy6
dummy7
dummy8
dummy9

; usermode definitions in lib, want access from code
;
unspaced
spaced
