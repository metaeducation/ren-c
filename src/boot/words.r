REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Canonical words"
    Rights: {
        Copyright 2012 REBOL Technologies
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Purpose: {
        These words are used internally by Rebol, and are canonized with small
        integer SYM_XXX constants.  These constants can then be quickly used
        in switch() statements.
    }
]

any-value! ;-- signal typesets start (SYM_ANY_VALUE_X hardcoded reference)
any-word!
any-path!
any-number!
any-scalar!
any-series!
any-string!
any-context!
any-array! ;-- replacement for ANY-BLOCK! that doesn't conflate with BLOCK!

;-----------------------------------------------------------------------------
; Signal that every earlier numbered symbol is for a typeset or datatype...

datatypes

; ...note that the words for types are created programmatically before
; this list is applied, so you only see typesets in this file.
;-----------------------------------------------------------------------------

; !!! Kept for functionality of #[none] in the loader for <r3-legacy>
none

; These aren't "quasiforms" in the bootstrap executable, they're words...but
; they're used in function specs and other places.
;
~
~void~
~null~

...  ; SYM_ELLIPSIS

generic ;-- used in boot, see %generics.r

export ;-- used in extensions

; The PICK action was killed in favor of a native that uses the same logic
; as path processing.  Code still remains for processing PICK, and ports or
; other mechanics may wind up using it...or path dispatch itself may be
; rewritten to use the PICK* action (but that would require significiant
; change for setting and getting paths)
;
; Similar story for POKE, which uses the same logic as PICK to find the
; location to write the value.
;
pick
poke

enfix
native
self
blank
true
false
trash
on
off
yes
no

rebol

system

; REFLECTORS
;
; These words are used for things like REFLECT SOME-FUNCTION 'BODY, which then
; has a convenience wrapper which is infix and doesn't need a quote, as OF.
; (e.g. BODY OF SOME-FUNCTION)
;
index
xy ;-- !!! There was an INDEX?/XY, which is an XY reflector for the time being
length
head
tail
head?
tail?
past?
open?
spec
body
words
values
types
title
binding
file
line
action
parent
near
label

; EVENT TYPES
;
read
wrote
connect
close
error
done
ready
lookup
custom


value ; used by TYPECHECKER to name the argument of the generated function

; !!! See notes on FUNCTION-META and SPECIALIZER-META in %sysobj.r
description
return-type
return-note
parameter-types
parameter-notes
specializee
specializee-name

x
y
+
-
*
unsigned
code        ; error field

;file -- already provided for FILE OF
dir

; Time:
hour
minute
second

; Date:
year
month
day
time
date
weekday
julian
yearday
zone
utc

; Used to recognize Rebol2 use of [catch] and [throw] in function specs
catch
throw

; Needed for processing of THROW's /NAME words used by system
; NOTE: may become something more specific than WORD!
exit
quit
;break ;-- covered by parse below
;return ;-- covered by parse below
continue

subparse ;-- recursions of parse use this for DECLARE_NATIVE(subparse) in backtrace

; PARSE - These words must not be reserved above!!  The range of consecutive
; index numbers are used by PARSE to detect keywords.
;
set  ; must be first first (SYM_SET referred to by GET_VAR() in %u-parse.c)
copy
across
some
any
repeat
opt
not
and
ahead
then
remove
insert
change
if
fail
reject
while
return
limit
seek
??
accept
break
; ^--prep words above
; v--match words below
skip
one
to
thru
quote
the
into
only
end  ; must be last (SYM_END referred to by GET_VAR() in %u-parse.c)

; Event:
type
key
port
mode
window
double
control
shift

; Checksum
sha1
md4
md5
crc32
adler32

; Codec actions
identify
decode
encode

bits

uid
euid
gid
egid
pid

;call/info
id
exit-code

; used when a function is executed but not looked up through a word binding
; (product of literal or evaluation) so no name is known for it
--anonymous--

; used to signal situations where information that would be available in
; a debug build has been elided
;
--optimized-out--

; used to indicate the execution point where an error or debug frame is
~~

include
source
library-path
runtime-path
options

; envelopes used with INFLATE and DEFLATE
;
zlib
gzip
detect

; REFLECT needs a SYM_XXX values at the moment, because it uses the dispatcher
; Generic_Dispatcher() vs. there being a separate one just for REFLECT.
; But it's not a type action, it's a native in order to be faster and also
; because it wants to accept nulls for TYPE OF () => null
;
reflect

; There was a special case in R3-Alpha for DECLARE_NATIVE(exclude) which wasn't an
; "ACTION!" (which meant no enum value) but it called a common routine that
; expected an action number.  So it passed zero.  Now that "type actions"
; use symbols as identity, this formalizes the hack by adding exclude.
;
exclude
