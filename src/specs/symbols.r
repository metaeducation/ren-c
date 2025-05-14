Rebol [
    system: "Ren-C Language Interpreter and Run-time Environment"
    title: "Symbols That want SYM_XXX IDs But Are Not In Lib"
    file: %symbols.r
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

        If new natives or otherwise are introduced which overlap with the
        names in this list, then the entry in this list must be removed.
        That means there is no ordering contract about the relative values
        of symbols in this file.  See %lib-words.r for that application.

        ISSUE! is used to denote items that should already exist.
    ]--
]

; === WORD!-based LOGIC ===

true
false
#on
#off
yes
no

; === SPECULATIVE ~NaN~ ===

NaN

errored  ; when rebRescue() has no handler and evaluates to non-fail ERROR!

optimized-out
anonymous


; See notes on ACTION-ADJUNCT in %sysobj.r
description


; PARAMETER! FIELDS AND VALUES

class  ; parameter.fields
escapable
#optional
#spec
text

normal  ; parameter classes
#meta
#the
#just


#x
#y
+
-
*
#code        ; error field

eval
#memory
; Time:
hour
minute
second

; Date:
#year
#month
#day
time
date
weekday
julian
yearday
#zone
utc


; === CHECKSUM (CORE) ===

crc32
adler32


; === CODEC ===

identify
decode
encode


...  ; SYM_ELLIPSIS_3

#bits

#data
#flags

id
exit-code

; used to signal situations where information that would be available in
; a RUNTIME_CHECKS build has been elided
;
--optimized-out--

; usage is to indicate the execution point where an error or debug frame is
**

; envelopes used with INFLATE and DEFLATE
;
zlib
gzip
detect

; Extensions use this for shutdown, when they are explicitly unloaded.
;
; !!! Plain modules do not have any shutdown mechanism, because they are not
; "unloaded".  This speaks to missing constructor/destructor mechanics.
;
unregister-extension*
shutdown*


; === EVENT TYPES ===
;
; !!! The event-types list used to be fixed and built into a separate enum.
; Now it is done with Rebol symbols.  Hence, these get uint16_t
; identifiers.  However, symbol numbers are hypothesized to be expandable
; via a pre-published dictionary of strings, committed to as a registry.


#open
#close
#connect
#accept  ; was R3 parse keyword
#read
#write
wrote
lookup

ready
#time

#show
#hide
