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

any-value!  ; signal typesets start (SYM_ANY_VALUE_X hardcoded reference)
any-word!
any-path!
any-number!
any-sequence!
any-tuple!
any-scalar!
any-series!
any-string!
any-context!
any-array!  ; replacement for ANY-BLOCK! that doesn't conflate with BLOCK!

;-----------------------------------------------------------------------------
; Signal that every earlier numbered symbol is for a typeset or datatype...

datatypes

; ...note that the words for types are created programmatically before
; this list is applied, so you only see typesets in this file.
;-----------------------------------------------------------------------------

;=== LEGACY HELPERS ===

none  ; !!! for LOAD #[none]
unset  ; !!! for LOAD #[unset]
image!  ; !!! for LOAD #[image! [...]] (used in tests), and molding, temporary
vector!  ; !!! for molding, temporary
gob!  ; !!! for molding, temporary
struct!  ; !!! for molding, temporary
library!  ; !!! for molding, temporary

generic  ; used in boot, see %generics.r

export  ; used in extensions

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
void
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
xy  ; !!! There was an INDEX?/XY, which is an XY reflector for the time being
;bytes  ; IMAGE! uses this to give back the underlying BINARY!--in %types.r
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
parameters
typesets
values
types
title
binding
file
line
action
near
label

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

; Secure:  (add to system/state/policies object too)
secure
protect
net
call
envr
eval
memory
debug
browse
extension
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
;break  ; covered by parse below
;return  ; covered by parse below
continue

subparse  ; recursions of parse use this for REBNATIVE(subparse) in backtrace

; PARSE - These words must not be reserved above!!  The range of consecutive
; index numbers are used by PARSE to detect keywords.
;
set  ; must be first first (SYM_SET referred to by GET_VAR() in %u-parse.c)
copy
collect  ; Variant in Red, but Ren-C's acts SET-like, suggested by @rgchris
keep
some
any
opt
not  ; turned to _not_ for SYM__NOT_, see TO-C-NAME for why this is weird
and  ; turned to _and_ for SYM__AND_, see TO-C-NAME for why this is weird
ahead  ; Ren-C addition (also in Red)
then
remove
insert
change
if  ; deprecated: https://forum.rebol.info/t/968/7
fail
reject
while
limit
seek  ; Ren-C addition
mark  ; Ren-C addition
??
|
accept
break
; ^--prep words above
    return  ; removed: https://github.com/metaeducation/ren-c/pull/898
; v--match words below
skip
to
thru
quote
literal  ; should both LIT and LITERAL be supported, or just LIT?
lit
lit-word!  ; !!! simulated datatype constraint (a QUOTED! like 'x)
lit-path!  ; !!! simulated datatype costraint (a QUOTED! like 'x/y)
refinement!  ; !!! simulated datatype constraint (a PATH! like `/word`)
predicate!  ; !!! simulated datatype constraint (a TUPLE! like `.word`)
blackhole!  ; !!! simulated datatype constraint (the ISSUE! `#`)
match
do
into
only
end  ; must be last (SYM_END referred to by GET_VAR() in %u-parse.c)

; It is convenient to be able to say `for-each [_ x y] [1 2 3 ...] [...]` and
; let the blank indicate you are not interested in a value.  This might be
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

; !!! The best implementation concept for LET would be a runtime concept
; where only those instances of LET in an evaluation stream could somehow add
; dynamically to a running context, while inert ones would not.  e.g.
; code like `data: [let x:]` would not create memory for an x variable unless
; you actually `do data`.  But as an interim step before the "virtual binding"
; needed for that exists, LET is searched for as a keyword by FUNC at
; function creation time, much like SET-WORD!s were looked for before.
;
let

; !!! Legacy: Used to report an error on usage of /LOCAL when <local> was
; intended.  Should be removed from code when the majority of such uses have
; been found, as the responsibility for that comes from %r2-warn.reb
;
local

; properties for action TWEAK function (passed as ISSUE!)
;
defer
postpone

; Event:
type
key
port
mode
window
double
control
shift

; Checksum (CHECKSUM-CORE only, others are looked up by string or libRebol)
crc32
adler32

; Codec actions
identify
decode
encode

; Serial parameters
; Parity
odd
even
; Control flow
hardware
software

; Struct
uint8
int8
uint16
int16
uint32
int32
uint64
int64
float
;double ;reuse earlier definition
pointer
raw-memory
raw-size
extern
rebval

~~~  ; !!! Temporary placeholder for ellipsis; will have to be special trick
varargs

; Gobs:
gob
offset
size
pane
parent
image
draw
text
effect
color
flags
rgb
alpha
data
resize
rotate
no-title
no-border
dropable
transparent
popup
modal
on-top
hidden
owner
active
minimize
maximize
restore
fullscreen

*port-modes*

; posix signal names
all
sigalrm
sigabrt
sigbus
sigchld
sigcont
sigfpe
sighup
sigill
sigint
sigkill
sigpipe
sigquit
sigsegv
sigstop
sigterm
sigtstp
sigttin
sigttou
sigusr1
sigusr2
sigpoll
sigprof
sigsys
sigtrap
sigurg
sigvtalrm
sigxcpu
sigxfsz

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
; type (provided by event)
kind
quotes

; There was a special case in R3-Alpha for REBNATIVE(exclude) which wasn't an
; "ACTION!" (which meant no enum value) but it called a common routine that
; expected an action number.  So it passed zero.  Now that "type actions"
; use symbols as identity, this formalizes the hack by adding exclude.
;
exclude

; !!! The SECURE feature in R3-Alpha was unfinished.  While the policies for
; security were conveyed with words, those words were mapped into an enum
; to pack as bit flags.  However, those bit flags have been moved to being
; internal to the security code...clients speak in terms of the symbols.
;
;read  ; covered above
;write  ; covered above
exec

; Actual underlying words for cells in lone `/` paths and lone `.` tuples,
; allowing binding and execution of operations like division or identity.
;
-slash-1-
-dot-1-
