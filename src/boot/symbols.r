REBOL [
    System: "Ren-C Language Interpreter and Run-time Environment"
    Title: "Symbols That want SYM_XXX IDs But Are Not In Lib"
    File: %symbols.r
    Rights: {
        Copyright 2012-2021 Ren-C Open Source Contributors
        Copyright 2012 REBOL Technologies
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Purpose: {
        These are words that are used internally by Rebol, which and are
        canonized with small integer SYM_XXX constants.  These constants can
        be quickly used in switch() statements, and a lookup table is on
        hand to turn SYM_XXX integers into the corresponding symbol pointer.

        If new natives or generics are introduced which overlap with the
        names in this list, then the entry in this list must be removed.
        That means there is no lordering contract about the relative values
        of symbols in this file.  See %lib-words.r for that application.

        ISSUE! is used to denote items that should already exist.
    }
]

; === NAMED BAD WORDS (SEE %sys-bad-word.h) ===

trash  ; only release build uses (debug build uses null as label to assert)

unset
#null
#blank
#false

void
stale  ; for non-/VOID DO, e.g. `(1 + 2 do [comment "hi"])` is ~stale~

errored  ; when rebRescue() has no handler and evaluates to non-fail ERROR!
rootvar  ; used as placeholder in rootvar cells
attached  ; answer from BINDING OF when variable is attached but unresolved
inherited  ; answer from BINDING OF when variable is attached and inherited


;=== LEGACY HELPERS ===

image!  ; !!! for LOAD #[image! [...]] (used in tests), and molding, temporary
vector!  ; !!! for molding, temporary
gob!  ; !!! for molding, temporary
struct!  ; !!! for molding, temporary
library!  ; !!! for molding, temporary


;=== COMBINATORS ===

input
#remainder
state


;=== REFLECTORS ===
;
; These words are used for things like REFLECT SOME-FUNCTION 'BODY, which then
; has a convenience wrapper which is infix and doesn't need a quote, as OF.
; (e.g. BODY OF SOME-FUNCTION)
;
type
kind
quotes
index
xy  ; !!! There was an INDEX?/XY, which is an XY reflector for the time being
;bytes  ; IMAGE! uses this to give back the underlying BINARY!--in %types.r
length
codepoint
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
exemplar
values
types
title
binding
attach
file
line
action
near
label

value ; used by TYPECHECKER to name the argument of the generated function

; See notes on ACTION-META in %sysobj.r
description
parameter-types
parameter-notes

x
y
+
-
*
unsigned
code        ; error field

eval
memory
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


; !!! Legacy: Used to report an error on usage of /LOCAL when <local> was
; intended.  Should be removed from code when the majority of such uses have
; been found, as the responsibility for that comes from %r2-warn.reb
;
local

; properties for action TWEAK function
;
barrier
defer
postpone

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

*** ; !!! Temporary placeholder for ellipsis; will have to be special trick
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

bits

uid
euid
gid
egid
pid

id
exit-code

; used to signal situations where information that would be available in
; a debug build has been elided
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
shutdown*


; === EVENT TYPES ===
;
; !!! The event-types list used to be fixed and built into a separate enum.
; Now it is done with Rebol symbols.  Hence, these get uint16_t
; identifiers.  However, symbol numbers are hypothesized to be expandable
; via a pre-published dictionary of strings, committed to as a registry.

; Event:
#type  ; declared in this file as a reflector
key
port
mode
window
double
control
#shift  ; there's a bit shift native

ignore          ; ignore event (0)
interrupt       ; user interrupt
device          ; misc device request
callback        ; callback event
custom          ; custom events
init

#open
#close
#connect
#accept  ; was R3 parse keyword
#read
#write
wrote
lookup

ready
done
#time

show
hide

move
down
up
alt-down
alt-up
aux-down
aux-up
#key
key-up

scroll-line
scroll-page

drop-file

error


; === EVENT KEYS ===

page-up
page-down
#end  ; parse keyword
home
left
#up
right
#down
#insert  ; GENERIC action insert
#delete  ; GENERIC action delete
f1
f2
f3
f4
f5
f6
f7
f8
f9
f10
f11
f12
