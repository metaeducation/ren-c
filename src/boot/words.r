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

; For the moment, TO-WORD of a datatype is willing to canonize a datatype
; as a word.  Long term, that specialization is not desirable because it
; is effectively building keywords deep into the system.  Better would be
; if datatypes could be communicated e.g. by #[()] for "groups" or "parens".
;
; Hardcoding in the GROUP! symbol is necessary for a legacy switch to be
; willing to convert a "group!" into the word GROUP!
;
paren!

; The PICK* action was killed in favor of a native that uses the same logic
; as path processing.  Code still remains for processing PICK*, and ports or
; other mechanics may wind up using it...or path dispatch itself may be
; rewritten to use the PICK* action (but that would require significiant
; change for setting and getting paths)
;
; Similar story for POKE, which uses the same logic as PICK* to find the
; location to write the value.
;
pick*
poke

native
action
self
blank
true
false
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
context
file
line
function

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
;break ;-- covered by parse below
;return ;-- covered by parse below
leave ;-- for PROC
continue

subparse ;-- recursions of parse use this for REBNATIVE(subparse) in backtrace

; PARSE - These words must not be reserved above!!  The range of consecutive
; index numbers are used by PARSE to detect keywords.
;
set ; must be first first (SYM_SET referred to by GET_VAR() in %u-parse.c)
copy
some
any
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
??
accept
break
; ^--prep words above
; v--match words below
skip
to
thru
quote
do
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

;routine
void
library
name
abi
stdcall
fastcall
sysv
thiscall
unix64
ms-cdecl
win64
default
vfp ;arm
o32; mips abi
n32; mips abi
n64; mips abi
o32-soft-float; mips abi
n32-soft-float; mips abi
n64-soft-float; mips abi
...
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

; used to signal a void in a reified va_list call, since voids can't actually
; appear in user-visible arrays
;
--void--

include
source
library-path
runtime-path
options
