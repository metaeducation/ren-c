REBOL [
	System: "REBOL [R3] Language Interpreter and Run-time Environment"
	Title: "Generate OS host API headers"
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

verbose: false

version: load %../boot/version.r

lib-version: version/3
print ["--- Make OS Ext Lib --- Version:" lib-version]

; Set platform TARGET
do %systems.r
target: config-system/os-dir

do %form-header.r

change-dir append %../os/ target

files: [
	%host-lib.c
	%../host-device.c
]

; If it is graphics enabled:
if all [
	not find any [system/options/args []] "no-gfx"
	find [3] system/version/4
][
	append files [%host-window.c]
]

cnt: 0

host-lib-externs: make string! 20000

host-lib-struct: make string! 1000

host-lib-instance: make string! 1000

rebol-lib-macros: make string! 1000
host-lib-macros: make string! 1000

;
; A checksum value is made to see if anything about the hostkit API changed.
; This collects the function specs for the purposes of calculating that value.
;
checksum-source: make string! 1000

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

process: func [file] [
	if verbose [?? file]
	data: read the-file: file
	data: to-string data ; R3
	parse/all data [
		any [
			thru "/***" 10 100 "*" newline
			thru "*/"
			copy spec to newline
			(if all [
				spec
				trim spec
				not find spec "static"
				fn: find spec "OS_"
				find spec #"("
			][
				; !!! We know `the-file`, but it's kind of noise to annotate
				append host-lib-externs reduce [
					"extern " spec ";" newline
				]
				append checksum-source spec
				p1: copy/part spec fn
				p3: find fn #"("
				p2: copy/part fn p3
				p2u: uppercase copy p2
				p2l: lowercase copy p2
				append host-lib-instance reduce [tab p2 "," newline]
				append host-lib-struct reduce [
					tab p1 "(*" p2l ")" p3 ";" newline
				]
				args: count p3 #","
				m: tail rebol-lib-macros
				append rebol-lib-macros reduce [
					{#define} space p2u args space {Host_Lib->} p2l args newline
				]
				append host-lib-macros reduce [
					"#define" space p2u args space p2 args newline
				]

				cnt: cnt + 1
			]
			)
			newline
			[
				"/*" ; must be in func header section, not file banner
				any [
					thru "**"
					[#" " | #"^-"]
					copy line thru newline
				]
				thru "*/"
				| 
				none
			]
		]
	]
]

append host-lib-struct {
typedef struct REBOL_Host_Lib ^{
	int size;
	unsigned int ver_sum;
	REBDEV **devices;
}

foreach file files [
	print ["scanning" file]
	if all [
		%.c = suffix? file
	][process file]
]

append host-lib-struct "} REBOL_HOST_LIB;"


;
; Do a reduce which produces the output string we will write to host-lib.h
;

out: reduce [

form-header/gen "Host Access Library" %host-lib.h %make-os-ext.r

newline

{#define HOST_LIB_VER} space lib-version newline
{#define HOST_LIB_SUM} space checksum/tcp to-binary checksum-source newline
{#define HOST_LIB_SIZE} space cnt newline

{
extern REBDEV *Devices[];

//
// While the host lib can call functions directly through host-lib-externs,
// it passes these hooks to Rebol by way of a table.  So if Rebol wants to
// call a function provided by the host lib, it does so by index in the
// table.  This is similar in spirit to how IOCTLs work in operating systems:
//
//	https://en.wikipedia.org/wiki/Ioctl
//
}

(host-lib-struct) newline

{
//** Included by HOST *********************************************

#ifndef REB_DEF

//
// Each function in the host lib has its own ordinary linkable C export,
// though Rebol's core code won't call the functions directly.  (It
// uses an offset into a table).  But these definitions are available
// so hostkit doesn't have to go through the table.
//
}

newline (host-lib-externs) newline

{
#ifdef OS_LIB_TABLE

//
// When Rebol is compiled with certain settings, the host-lib.h file acts
// more like a .inc file, declaring a table instance.  Multiple inclusions
// under this mode will generate duplicate Host_Lib symbols, so beware!
//
// !!! As with other things from the 12-Dec-2012 Rebol open sourcing, this
// technique of declaring a variable instance in a header file (which is
// known to be a questionable practice in C) should be re-evaluated.
//

REBOL_HOST_LIB *Host_Lib;
}

newline 

"REBOL_HOST_LIB Host_Lib_Init = {"

{
	HOST_LIB_SIZE,
	(HOST_LIB_VER << 16) + HOST_LIB_SUM,
	(REBDEV**)&Devices,
}

(host-lib-instance)

"^};" newline

newline

{#endif //OS_LIB_TABLE

//
// Although Rebol and hostkit code have different ways of calling the same
// functions, this difference is abstracted away via a table of macros.
// So if you have a function like OS_Alloc_Mem exported by the hostkit, in
// Rebol you would call this as `Host_Lib->os_alloc_mem(...)`, while on the
// hostkit side you'd just call `OS_Alloc_Mem(...)` directly.  By creating
// an OS_ALLOC_MEM macro that does the right thing regardless, one can code
// using a common style inside of Rebol or outside.  This does the all-caps
// defines for the host case.  (But in the case of OS_ALLOC_MEM, use either
// OS_ALLOC or OS_ALLOC_ARRAY, which are more safe!)
//
}

newline (host-lib-macros) newline

{
#else //REB_DEF

//** Included by REBOL ********************************************

//
// As mentioned above, there are different ways that Rebol internally
// connects to the hostkit (via table) and externally (via function
// pointer.  These all-caps forms of the names as a macro glosses
// that distinction so you can stylize your code the same either way.
//
}

newline newline (rebol-lib-macros)

{
extern REBOL_HOST_LIB *Host_Lib;

#endif //REB_DEF

/***********************************************************************
**
**	"OS" MEMORY ALLOCATION AND FREEING MACROS
**
**		For better or for worse, Rebol's hostkit didn't want to expose
**		the interpreter's internal and pooled Alloc_Mem or Free_Mem.
**		(see the comments in reb-c.h on NEW and DELETE about that).
**		The theory was that the host kit shouldn't use malloc in
**		general, due to an idea that some hosts would have their
**		own special non-malloc allocators to offer.  Hence OS_Make
**		and OS_Free were provided (wrapped as OS_MAKE and OS_FREE).
**
**		Of course, since malloc() and free() couldn't be hidden or
**		disabled, things outside of the interpreter core would use
**		them anyway.  GUI or network code wasn't verified to not
**		use it.
**
**		Over the long run, it's not clear what the advantages of
**		these OS_Alloc_Mem and OS_Free_Mem functions actually are.
**		Perhaps Rebol should expose its internal pooled allocation?
**		Or any host kit code needs to just fend for itself and find
**		its own path toward allocation vs. expecting Rebol to
**		give it the hook-up.  Until that is figured out, these
**		macros help make the host kit's allocation parallel more
**		obvious.
**
**		!!! Note that unlike Free_Mem, OS_FREE_MEM does not currently
**		require you to remember the size of the allocation to
**		pass in.  OS_FREE does it for symmetry with FREE.
**
**		!!! As a first step in defining these macros to work with C++
**		only the allocations were updated to get type correctness.
**		Many routines are left using OS_Free_Mem instead of OS_FREE.
**
***********************************************************************/

// WARNING! auto-generated by make-host-ext.r
#define OS_ALLOC(t) \
	r_cast(t *, OS_ALLOC_MEM(sizeof(t)))

// WARNING! auto-generated by make-host-ext.r
#define OS_ALLOC_ZEROFILL(t) \
	r_cast(t *, memset(OS_ALLOC(t), '\0', sizeof(t)))

// WARNING! auto-generated by make-host-ext.r
#define OS_ALLOC_ARRAY(t,n) \
	r_cast(t *, OS_ALLOC_MEM(sizeof(t) * (n)))

// WARNING! auto-generated by make-host-ext.r
#define OS_ALLOC_ARRAY_ZEROFILL(t,n) \
	r_cast(t *, memset(OS_ALLOC_ARRAY(t, (n)), '\0', sizeof(t) * (n)))

#if defined(__cplusplus) && __cplusplus >= 201103L
	// In C++11, decltype lets us do a bit more sanity checking that the
	// C-oriented API is actually freeing the data type it thinks that it is

	#include <type_traits>

	// WARNING! auto-generated by make-host-ext.r
	#define OS_FREE(t,p) \
		do { \
			static_assert( \
				std::is_same<decltype(p), std::add_pointer<t>::type>::value, \
				"mismatched OS_FREE type" \
			); \
			OS_FREE_MEM(p, sizeof(t)); \
		} while (0)

	// WARNING! auto-generated by make-host-ext.r
	#define OS_FREE_ARRAY(t,n,p) \
		do { \
			static_assert( \
				std::is_same<decltype(p), std::add_pointer<t>::type>::value, \
				"mismatched OS_DELETE_ARRAY type" \
			); \
			OS_FREE_MEM(p); \
		} while (0)
#else
	// WARNING! auto-generated by make-host-ext.r
	#define OS_FREE(t,p) \
		OS_FREE_MEM(p)

	// WARNING! auto-generated by make-host-ext.r
	#define OS_FREE_ARRAY(t,n,p) \
		OS_FREE_MEM(p)
#endif
}
]

;print out ;halt
;print ['checksum checksum/tcp checksum-source]
write %../../include/host-lib.h out
;ask "Done"
print "   "
