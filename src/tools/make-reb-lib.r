REBOL [
	System: "REBOL [R3] Language Interpreter and Run-time Environment"
	Title: "Make Reb-Lib related files"
	Rights: {
		Copyright 2012 REBOL Technologies
		REBOL is a trademark of REBOL Technologies
	}
	License: {
		Licensed under the Apache License, Version 2.0
		See: http://www.apache.org/licenses/LICENSE-2.0
	}
	Author: "Carl Sassenrath"
	Needs: 2.100.100
]

print "--- Make Reb-Lib Headers ---"

verbose: true

lib-ver: 2

preface: "RL_"

src-dir: %../core/
reb-lib: src-dir/a-lib.c
ext-lib: src-dir/f-extension.c

out-dir: %../include/
reb-ext-lib:  out-dir/reb-lib.h   ; for Host usage
reb-ext-defs: out-dir/reb-lib-lib.h  ; for REBOL usage

ver: load %../boot/version.r

do %common.r

do %form-header.r

;-----------------------------------------------------------------------------
;-----------------------------------------------------------------------------

proto-count: 0

xlib-buffer: make string! 20000
rlib-buffer: make string! 1000
mlib-buffer: make string! 1000
dlib-buffer: make string! 1000
comments-buffer: make string! 1000
xsum-buffer: make string! 1000

emit:  func [d] [append repend xlib-buffer d newline]
emit-rlib: func [d] [append repend rlib-buffer d newline]
emit-dlib: func [d] [append repend dlib-buffer d newline]
emit-comment: func [d] [append repend comments-buffer d newline]
emit-mlib: func [d /nol] [
	repend mlib-buffer d
	if not nol [append mlib-buffer newline]
]

count: func [s c /local n] [
	if find ["()" "(void)"] s [return "()"]
	out: copy "(a"
	n: 1
	while [s: find/tail s c][
		repend out [#"," #"a" + n]
		n: n + 1
	]
	append out ")"
]

in-sub: func [text pattern /local position] [
	all [
		position: find text pattern ":"
		insert position "^/:"
		position: find next position newline
		remove position
		insert position " - "
	]
]

gen-doc: func [name proto text] [
	replace/all text "**" "  "
	replace/all text "/*" "  "
	replace/all text "*/" "  "
	trim text
	append text newline

	insert find text "Arguments:" "^/:"
	bb: beg: find/tail text "Arguments:"
	insert any [find bb "notes:" tail bb] newline
	while [
		all [
			beg: find beg " - "
			positive? offset-of beg any [find beg "notes:" tail beg]
		]
	][
		insert beg </tt>
		insert find/tail/reverse beg newline {<br><tt class=word>}
		beg: find/tail beg " - "
	]

	beg: insert bb { - } ;<div style="white-space: pre;">}
	remove find beg newline
	remove/part find beg "<br>" 4 ; extra <br>

	remove find text "^/Returns:"
	in-sub text "Returns:"
	in-sub text "Notes:"

	insert text reduce [
		":Function: - " <tt class=word> proto </tt>
		"^/^/:Summary: - "
	]
	emit-comment ["===" name newline newline text]
]

pads: func [start col] [
	col: col - offset-of start tail start
	head insert/dup clear "" #" " col
]

emit-proto: funct/extern [proto] [

	if all [
		proto
		trim proto
		pos.id: find proto preface
		find proto #"("
	] [
		emit ["RL_API " proto ";"] ;    // " the-file]
		append xsum-buffer proto
		fn.declarations: copy/part proto pos.id
		pos.lparen: find pos.id #"("
		fn.name: copy/part pos.id pos.lparen
		fn.name.upper: uppercase copy fn.name
		fn.name.lower: lowercase copy find/tail fn.name preface

		emit-dlib [tab fn.name ","]

		emit-rlib [tab fn.declarations "(*" fn.name.lower ")" pos.lparen ";"]

		args: count pos.lparen #","
		mlib.tail: tail mlib-buffer
		emit-mlib/nol ["#define " fn.name.upper args]
		emit-mlib [pads mlib.tail 35 " RL->" fn.name.lower args]
		if pos.starredline: find comment-text "****" [
			append clear pos.starredline "*/"
		]
		emit-mlib ["/*^/**^-" proto "^/**" comment-text newline]

		gen-doc fn.name proto comment-text

		proto-count: proto-count + 1
	]
][proto-count]

func-header: [
	;-- WARNING: as written this means you can't use RL_API in a comment
	;-- or this will screw up... more rigor needed.

	thru "RL_API " copy proto to newline skip
	opt ["/*" copy comment-text thru "*/"]
	(emit-proto proto)
	newline
	opt [
		"/*" ; must be in func header section, not file banner
		any [
			thru "**" [#" " | #"^-"] copy line thru newline
		]
		thru "*/"
	]
]

process: func [file] [
	if verbose [?? file]
	data: to string! read the-file: file ;R3
	parse data [
		any func-header
	]
]

write-if: func [file data] [
	if data <> attempt [to string! read file][ ;R3
		print ["UPDATE:" file]
		write file data
	]
]

;-----------------------------------------------------------------------------

emit-rlib {
typedef struct rebol_ext_api ^{}

emit-comment [{Host/Extension API

=r3

=*Updated for A} ver/3 { on } now/date {

=*Describes the functions of reb-lib, the REBOL API (both the DLL and extension library.)

=!This document is auto-generated and changes should not be made within this wiki.

=note WARNING: PRELIMINARY Documentation

=*This API is under development and subject to change. Various functions may be moved, removed, renamed, enhanced, etc.

Also note: the formatting of this document will be enhanced in future revisions.

=/note

==Concept

The REBOL API provides common API functions needed by the Host-Kit and also by
REBOL extension modules. This interface is commonly referred to as "reb-lib".

There are two methods of linking to this code:

*Direct calls as you would use functions within any DLL.

*Indirect calls through a set of macros (that use a structure pointer to the library.)

==Functions
}]

;-----------------------------------------------------------------------------

process reb-lib
process ext-lib

;-----------------------------------------------------------------------------

emit-rlib "} RL_LIB;"

out: to-string reduce [
form-header/gen "REBOL Host and Extension API" %reb-lib.r %make-reb-lib.r
{
// These constants are created by the release system and can be used to check
// for compatiblity with the reb-lib DLL (using RL_Version.)
#define RL_VER } ver/1 {
#define RL_REV } ver/2 {
#define RL_UPD } ver/3 {

// Compatiblity with the lib requires that structs are aligned using the same
// method. This is concrete, not abstract. The macro below uses struct
// sizes to inform the developer that something is wrong.
#if defined(__LP64__) || defined(__LLP64__)

#ifdef HAS_POSIX_SIGNAL
	#define CHECK_STRUCT_ALIGN (sizeof(REBREQ) == 196 && sizeof(REBEVT) == 16)
#else
	#define CHECK_STRUCT_ALIGN (sizeof(REBREQ) == 100 && sizeof(REBEVT) == 16)
#endif //HAS_POSIX_SIGNAL

#else // !defined(__LP64__) && !defined(__LLP64__)

#ifdef HAS_POSIX_SIGNAL
	#define CHECK_STRUCT_ALIGN (sizeof(REBREQ) == 180 && sizeof(REBEVT) == 12)
#else
	#define CHECK_STRUCT_ALIGN (sizeof(REBREQ) == 80 && sizeof(REBEVT) == 12)
#endif //HAS_POSIX_SIGNAL

#endif

// Function entry points for reb-lib (used for MACROS below):}
rlib-buffer
{
// Extension entry point functions:
#ifdef TO_WINDOWS
	#define RXIEXT __declspec(dllexport)
#else
	#define RXIEXT extern
#endif

#ifdef __cplusplus
extern "C" ^{
#endif

RXIEXT const char *RX_Init(int opts, RL_LIB *lib);
RXIEXT int RX_Quit(int opts);
RXIEXT int RX_Call(int cmd, RXIFRM *frm, void *data);

// The macros below will require this base pointer:
extern RL_LIB *RL;  // is passed to the RX_Init() function

// Macros to access reb-lib functions (from non-linked extensions):

}
mlib-buffer
{

#define RL_MAKE_BINARY(s) RL_MAKE_STRING(s, FALSE)

#ifndef REB_EXT // not extension lib, use direct calls to r3lib

}
xlib-buffer
{
#endif // REB_EXT

#ifdef __cplusplus
^}
#endif

}
]

write-if reb-ext-lib out

;-----------------------------------------------------------------------------

out: to-string reduce [
form-header/gen "REBOL Host/Extension API" %reb-lib-lib.r %make-reb-lib.r
{RL_LIB Ext_Lib = ^{
}
dlib-buffer
{^};
}
]

write-if reb-ext-defs out

write-if %../reb-lib-doc.txt comments-buffer

;ask "Done"
print "   "
