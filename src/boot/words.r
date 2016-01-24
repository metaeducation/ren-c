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
        These words are used internally by REBOL and must have specific canon
        word values in order to be correctly identified.
    }
]

any-nothing! ;-- signal typesets start (SYM_ANY_NOTHING_X hardcoded reference)
any-something!
any-value!
opt-any-value! ;-- temporary until <opt> or #opt or similar are settled
any-word!
any-path!
any-function!
any-number!
any-scalar!
any-series!
any-string!
any-context!
any-array! ;-- replacement for any-block! that doesn't conflate with "block"

;-----------------------------------------------------------------------------
; Signal that every earlier numbered symbol is for a typeset or datatype...

datatypes

; ...note that the words for types are created programmatically before
; this list is applied, so you only see typesets in this file.
;-----------------------------------------------------------------------------

; For the moment, TO-WORD of a datatype is willing to canonize a datatype
; as a word.  Long term, that specialization is not desirable because it
; is effectively building keywords deep into the system.  Better would be
; if datatypes could be communicated e.g. by #[()] for "groups" or "parens".
;
; Hardcoding in the GROUP! symbol is necessary for a legacy switch to be
; willing to convert a "group!" into the word GROUP!
;
paren!

native
action
self
none
true
false
on
off
yes
no
pi

rebol

system

;boot levels
base
sys
mods

;reflectors:
spec
body
words
values
types
title

x
y
+
-
*
unsigned
code        ; error field
delect

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
;dir - below
;file - below

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

; Schemes
console
file
dir
event
callback
dns
tcp
udp
clipboard
serial
signal

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
addr
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
crash
crash-dump
watch-recycle
watch-obj-copy
stack-size

uid
euid
gid
egid
pid

;call/info
id
exit-code

; used as error fields in debug builds for C's __FILE__ and __LINE__ of origin
__FILE__
__LINE__

; required by OPTIONS_ARG1_ARG2_ARG3_ERROR, assumed to be sequential symbol #s
arg1
arg2
arg3
