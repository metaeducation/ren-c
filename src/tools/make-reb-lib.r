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
	print ["scanning" file "in" what-dir]
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
// So if you have a function like OS_Get_Time exported by the hostkit, in
// Rebol you would call this as `Host_Lib->os_get_time(...)`, while on the
// hostkit side you'd just call `OS_Get_Time(...)` directly.  By creating
// an OS_GET_TIME macro that does the right thing regardless, one can code
// using a common style inside of Rebol or outside.
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
// For this reason, you should generally always use the all-caps
// versions and not the mixed-case ones.  It eases the ability to
// copy code snippets into-and-out-of Rebol Core.
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
**		In communicating with the host, it is necessary for Rebol to
**		be able to allocate memory the host would be responsible
**		for freeing (or at least, possibly responsible).  The reverse
**		is also true; sometimes the host needs to hand a filename
**		to Rebol (or to a port model / extension that Rebol brokers.)
**
**		This could have been done with malloc() and free()... but
**		a hook was added that allowed Rebol to not make that
**		assumption.  This was primarily to open doors for embedded
**		systems with custom allocators.  So since the host and Rebol
**		were already communicating through a table of functions,
**		two of those could be used for allocating and freeing so
**		Rebol wasn't "canonizing" malloc.
**
**		The two hooks are named OS_Alloc_Mem and OS_Free_Mem.  The
**		preferred method for invoking them is through this set of
**		all-caps macros and not OS_ALLOC_MEM or OS_FREE_MEM:
**
**		1. They produce type-safe results, as the allocations ask you
**		to pass a type as the first parameter.  You don't need casts
**		in the client as `OS_ALLOC(SomeType)` will give back a
**		`SomeType*` which points to a block of memory of
**		`sizeof(SomeType)`.  That provides greater compatibility
**		with C++ as well.
**
**		2. They match up with the design of the non-OS `ALLOC()` and
**		`FREE()` functions that are used inside Rebol for its own
**		pooled memory.  Familiarity with one provides a rhythm of
**		using the other; and inside of Rebol it's more important to
**		use them because the calls to FREE need to provide a size.
**		It's also easy to switch it up, to take the OS_ off or tack
**		it on if it's decided a block doesn't need to cross a boundary.
**
**		3. Just plain nicer to write `OS_ALLOC(SomeType)` than
**		having to say `(SomeType*)OS_Alloc_Mem(sizeof(SomeType))`
**
**		While it is not necessary for hostkit code to use the OS_ALLOC
**		it gives to Rebol for the bridging purposes, it might not
**		be a bad choice if there aren't other requirements from
**		external libraries.  However, memory that is allocated with
**		malloc should not be freed with OS_FREE or vice-versa,
**		even "if you know" that OS_Free_Mem is implemented as free().
**		It's best if you use debug checks or tricks to enforce this.
**		See Ren/C's m-pools.c for an example.
**
**		!!! Note that unlike Alloc_Mem insie Rebol Core, OS_ALLOC_MEM
**		keeps track of the size on its own and can free it.  Hence
**		there are no analogs to the FREE_ARRAY for an OS_FREE_ARRAY
**		necessary...just always use OS_FREE.
**
***********************************************************************/

// !!!!! DO NOT EDIT THESE DEFINITIONS FROM WITHIN host-lib.h !!!!!
// v--- (nor others, this file is generated by make-reb-lib.r) ^^---

#define OS_ALLOC(t) \
	r_cast(t *, OS_ALLOC_MEM(sizeof(t)))
#define OS_ALLOC_ZEROFILL(t) \
	r_cast(t *, memset(OS_ALLOC(t), '\0', sizeof(t)))
#define OS_ALLOC_ARRAY(t,n) \
	r_cast(t *, OS_ALLOC_MEM(sizeof(t) * (n)))
#define OS_ALLOC_ARRAY_ZEROFILL(t,n) \
	r_cast(t *, memset(OS_ALLOC_ARRAY(t, (n)), '\0', sizeof(t) * (n)))
#define OS_FREE(p) \
	OS_FREE_MEM(r_cast(void *, p))

// !!!!! DO NOT EDIT THESE DEFINITIONS FROM WITHIN host-lib.h !!!!!
// ^^--- (nor others, this file is generated by make-reb-lib.r) v---



/***********************************************************************
**
**  VARIANT CHARACTER TYPE BASED ON OPERATING SYSTEM
**
**		REBCHR is the idea of the size of a character that the OS uses.
**		Currently the supported platforms either use wide characters
**		(e.g. Windows with `typedef wchar_t WCHAR`) or just use ordinary
**		`char` like Linux and OS/X do for UTF-8.
**
**		Windows actually does `typedef wchar_t WCHAR` and then its binary
**		APIs depend on this size.  Hence any compiler for Windows must
**		have a 2-byte definition for wchar_t, or your programs won't work.
**		This means Windows programmers can confidently use routines like
**		wcslen() on strings they are passed from the OS.
**
**		Linux and OS/X haven't gone this direction.  Their APIs still use
**		plain chars, often passing-the-buck on who-and-where-and-how the
**		bytes will be encoded or decoded.  There aren't guarantees about
**		the wchar_t size.  It's often 4 bytes on 64-bit platforms:
**
** 			http://stackoverflow.com/questions/3877052/
**
**		While REBCHR is a synonym for the native char type in hostkit
**		code (e.g. #including <reb-host.h> vs. <sys-core.h>) there is
**		something crafty about it when used in core.  On builds for
**		OSes with non-wide characters, it is there set to a typedef
**		for an unsigned REBYTE instead of a char (which is *usually*
**		signed.  Leveraging the same sign checking as REBYTE for UTF-8
**		helps to avoid accidental strlen() calls that bypass the
**		attempt for Rebol to be platform-neutral...checking coders on
**		their wide-character support even when building for the likes
**		of Linux or OS/X.
**
**		As a result, inside of Rebol it has to treat REBCHR as something
**		of a black box.  Only a few proxies are supplied here, with names
**		matching the C library equivalents (like `OS_STRLEN`).  These
**		are also exported to hostkit code...which may seem strange as
**		the type is not opaque to them and they could use their native
**		strlen() or wcslen() without a problem on a REBCHR*.  Yet
**		sometimes on the host side, one wants to write code that is
**		reusable between platforms.  Hence that code has to remain
**		about as agnostic about what a REBCHR is as the core itself.
**
**		!!! Note the non-Wide versions use universal casts to get their
**		argument from what could be either a `const REBCHR*` or a `REBCHR`
**		to either the `const char*` or `char*` the parameter wants.
**		Working with macros and casting, there's not much else that can
**		be done; so this won't check const correctness or that you're
**		passing in some REBCHR* at all.  While that is unfortunate, a
**		wide-character build would be able to check common code...and the
**		code that's in the specialized host code would use their native
**		routines on the non-opaque type and be safe.
**
**		!!! (One "crazy" idea could be to turn off the signed conversion
**		warnings in release builds...but keep the const checks.  Then
**		make cs_cast, s_cast, b_cast, and cb_cast no-ops.  That would
**		check this one problem; weird but worth thinking about esp.
**		in an automated build.)
**
**		For why OpenBSD uses strlcpy/strlcat as "Less error-prone":
**
**			https://www.freebsd.org/cgi/man.cgi?query=strlcpy&sektion=3
**
***********************************************************************/

#ifdef OS_WIDE_CHAR

	// !!!!! DO NOT EDIT THESE DEFINITIONS FROM WITHIN host-lib.h !!!!!
	// v--- (nor others, this file is generated by make-reb-lib.r) ^^---
	#define OS_WIDE				TRUE
	#define OS_STR_LIT(s)		(L##s)
	#define OS_STRNCPY(d,s,n)	wcsncpy((d),(s),(n))
	#define OS_STRNCAT(d,s,n)	wcsncat((d),(s),(n))
	#define OS_STRSTR(d,s)		wcsstr((d),(s))
	#define OS_STRCHR(d,s)		wcschr((d),(s))
	#define OS_STRLEN(s)		wcslen(s)
	// !!!!! DO NOT EDIT THESE DEFINITIONS FROM WITHIN host-lib.h !!!!!
	// ^^--- (nor others, this file is generated by make-reb-lib.r) v---

#else

	// !!!!! DO NOT EDIT THESE DEFINITIONS FROM WITHIN host-lib.h !!!!!
	// v--- (nor others, this file is generated by make-reb-lib.r) ^^---
	#define OS_WIDE FALSE
	#define OS_STR_LIT(s) cb_cast(s)
	#ifdef TO_OBSD
		#define OS_STRNCPY(d,s,m) \
			b_cast(strlcpy((char *)(d), (const char *)(s), (m))
		#define OS_STRNCAT(d,s,m) \
			b_cast(strlcat((char *)(d), (const char *)(s), (m))
	#else
		#define OS_STRNCPY(d,s,n) \
			b_cast(strncpy((char *)(d), (const char *)(s), (n)))
		#define OS_STRNCAT(d,s,n) \
			b_cast(strncat((char *)(d), (const char *)(s), (n)))
	#endif
	#define OS_STRSTR(d,s) \
		b_cast(strstr((char *)(d), (const char *)(s)))
	#define OS_STRCHR(d,s) \
		b_cast(strchr((char *)(d), s))
	#define OS_STRLEN(s) \
		strlen((const char *)(s))
	// !!!!! DO NOT EDIT THESE DEFINITIONS FROM WITHIN host-lib.h !!!!!
	// ^^--- (nor others, this file is generated by make-reb-lib.r) v---

#endif
}
]

;print out ;halt
;print ['checksum checksum/tcp checksum-source]
write %../../include/host-lib.h out
;ask "Done"
print "   "
