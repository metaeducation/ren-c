REBOL [
    System: "Ren-C Language Interpreter and Run-time Environment"
    Title: "Extension words that have IDs, but strings aren't in core EXE"
    File: %ext-words.r
    Rights: --{
        Copyright 2025 Ren-C Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    }--
    License: --{
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }--
    Purpose: --{
        The idea behind this file is that there may be some words which are
        not used by the core, but that extensions use...and would benefit
        from being able to recognize by an integer symbol ID.

        However, we don't want to bloat the executable in cases where you
        are not using those words, by packing in the strings for defining
        the words.

        This approach is that there is a published and agreed upon mapping of
        symbols to numbers.  When an extension loads, it provides the string
        and the number, which is then written onto the symbol.  So long as
        all extensions register the same symbol with the same number, there
        is no problem.

        Hence the extension can use compile-time constants that they define
        for EXT_SYM_XXX words which are not built in.
    }--
    Notes: --{
        * Any words in this file that are committed to must keep those IDs.
          So if the core extends to need that word it must use the same
          registration process that extensions would.  Hence the commitment
          shouldn't be made until it's absolutely necessary (e.g. some far
          future where third parties make extensions that can't be broken
          for binary compatibility).  The list could be renegotiated at the
          moment of any change that required extensions using the core API
          to be recompiled anyway, allowing more builtin symbols to fold in
          the executable such that CANON(...) works with them.

        * As with %symbols.r and %lib-words.r, ISSUE! in this file marks a
          word that logically belongs as part of a set but has been defined
          previously for other purposes, so it won't be defined again.
    }--
]

[=== image ===]

image!
rgb
alpha
#size


[=== process ===]

uid
euid
gid
egid
pid


[=== ffi ===]

struct!
unsigned
uint8
int8
uint16
int16
uint32
int32
uint64
int64
float
double
pointer
rebval  ; would "cell" be better?  probably so...

routine?
#void
#return
library
name
; #...  ; for variadics, #... is an invalid issue :-(
varargs  ; still needed?

raw-memory
raw-size
extern
alignment

; checking ABI settings currently done with librebol, not EXT_SYM_XXX switch()
abi
#default
win64
sysv
stdcall
thiscall
fastcall
ms-cdecl
unix64
vfp  ; arm
o32  ; mips abi
n32  ; mips abi
n64  ; mips abi
o32-soft-float  ; mips abi
n32-soft-float  ; mips abi
n64-soft-float  ; mips abi


[=== vector ===]

#unsigned


[=== gui ===]

window

key
mode
control
#shift  ; there's a bit shift native
alt-down
alt-up
aux-down
aux-up
#key
key-up
scroll-line
scroll-page
page-up
page-down
#end  ; parse keyword
home
up
down
left
right
#insert  ; GENERIC action insert
#delete  ; GENERIC action delete
f1  ; should these be capitalized, or capitalized variants offered?  F1, etc?
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

#double
click
mouse
move
#up  ; keyboard has up
#down  ; keyboard has down

drop-file


[=== event ===]

ignore
interrupt
device
callback
custom
init


[=== signal ===]

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
