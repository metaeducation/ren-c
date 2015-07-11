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
; (Ren/C is a core build independent of graphics, so it never will be)
comment [
	if all [
		not find any [system/options/args []] "no-gfx"
		find [3 4] system/version/4
	][
		append files [%host-window.c %host-graphics.c]
	]
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

				;-- !!! All functions *should* start with OS_, not just
				;-- have OS_ somewhere in it!  At time of writing, Atronix
				;-- has added As_OS_Str and when that is addressed in a
				;-- later commit to OS_STR_FROM_SERIES (or otherwise) this
				;-- backwards search can be removed
				fn: next find/reverse fn space
				fn: either #"*" = first fn [next fn] [fn]

				find spec #"("
			][
				; !!! We know 'the-file', but it's kind of noise to annotate
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

/***********************************************************************
**
**	HOST LIB TABLE INITIALIZATION
**
**		!!!
**		!!! **WARNING!**  DO NOT EDIT THIS! (until you've checked...)
**		!!! BE SURE YOU ARE EDITING MAKE-OS-EXT.R AND NOT HOST-LIB.H
**		!!!
**
**		The "Rebol Host" provides a "Host Lib" interface to operating
**		system services that can be used by "Rebol Core".  Each host
**		provides functions with names starting with OS_ and then a
**		mixed-case name separated by underscores (e.g. OS_Get_Time).
**
**		Rebol cannot call these functions directly.  Instead, they are
**		put into a table (which is actually a struct whose members are
**		function pointers of the appropriate type for each call).  It is
**		similar in spirit to how IOCTLs work in operating systems:
**
**			https://en.wikipedia.org/wiki/Ioctl
**
**		To give a sense of scale, there are 48 separate functions in the
**		Linux build at time of writing.  Some functions are very narrow
**		in what they do...such as OS_Browse which will open a web browser.
**		Other functions are doorways to dispatching a wide variety of
**		requests, such as OS_Do_Device.)
**
**		So instead of OS_Get_Time, Core uses 'Host_Lib->os_get_time(...)'.
**		Since that is verbose, an all-caps macro is provided, which in
**		this case would be OS_GET_TIME.  For parity, all-caps macros are
**		provided in the host like '#define OS_GET_TIME OS_Get_Time'.  As
**		a result, the all-caps forms should be preserved since they can
**		be read/copied/pasted consistently between host and core code.
**
**		!!!
**		!!! **WARNING!**  DO NOT EDIT THIS! (until you've checked...)
**		!!! BE SURE YOU ARE EDITING MAKE-OS-EXT.R AND NOT HOST-LIB.H
**		!!!
**
***********************************************************************/
}

(host-lib-struct) newline

{
//** Included by HOST *********************************************

#ifndef REB_DEF
}

newline (host-lib-externs) newline

{
#ifdef OS_LIB_TABLE

/***********************************************************************
**
**	HOST LIB TABLE INITIALIZATION
**
**		When Rebol is compiled with certain settings, the host-lib.h
**		file acts more like a .inc file, declaring a table instance.
**		Multiple inclusions under this mode will generate duplicate
**		Host_Lib symbols, so beware!  There's likely a better place
**		to put this or a better way to do it, but it's how it was
**		when Rebol was open-sourced and has not been changed yet.
**
**		!!!
**		!!! **WARNING!**  DO NOT EDIT THIS! (until you've checked...)
**		!!! BE SURE YOU ARE EDITING MAKE-OS-EXT.R AND NOT HOST-LIB.H
**		!!!
**
***********************************************************************/

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
}

newline (host-lib-macros) newline

{
#else //REB_DEF

//** Included by REBOL ********************************************

}

newline newline (rebol-lib-macros)

{
extern REBOL_HOST_LIB *Host_Lib;

#endif //REB_DEF
}
]

;print out ;halt
;print ['checksum checksum/tcp checksum-source]
write %../../include/host-lib.h out
;ask "Done"
print "   "
