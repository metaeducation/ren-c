REBOL [
	System: "REBOL [R3] Language Interpreter and Run-time Environment"
	Title: "Make primary boot files"
	Rights: {
		Copyright 2012 REBOL Technologies
		REBOL is a trademark of REBOL Technologies
	}
	License: {
		Licensed under the Apache License, Version 2.0
		See: http://www.apache.org/licenses/LICENSE-2.0
	}
	Author: "Carl Sassenrath"
	Version: 2.100.0
	Needs: 2.100.100
	Purpose: {
		A lot of the REBOL system is built by REBOL, and this program
		does most of the serious work. It generates most of the C include
		files required to compile REBOL.
	}
]

print "--- Make Boot : System Embedded Script ---"

do %common.r

do %form-header.r

do %systems.r
config: config-system/guess system/options/args

write-if: func [file data] [
	if data <> attempt [read file][
		print ["UPDATE:" file]
		write file data
	]
]

;-- SETUP --------------------------------------------------------------

change-dir %../boot/
;dir: %../core/temp/  ; temporary definition
inc: %../include/
src: %../core/

version: load %version.r
version/4: config/id/2
version/5: config/id/3

;-- Title string put into boot.h file checksum:
Title:
{REBOL
Copyright 2012 REBOL Technologies
REBOL is a trademark of REBOL Technologies
Licensed under the Apache License, Version 2.0
}

sections: [
	boot-types
	boot-words
	boot-root
	boot-task
	boot-strings
	boot-booters
	boot-actions
	boot-natives
	boot-ops
	boot-typespecs
	boot-errors
	boot-sysobj
	boot-base
	boot-sys
	boot-mezz
	boot-protocols
;	boot-script
]

include-protocols: false      ; include protocols in build

;-- Error handler:
error: func [msg arg] [print ["*** Make-boot error:" msg arg] halt]

;-- Args passed: platform, product
if none? args: system/options/args [error "No platform specified." ""]

if args/1 = ">" [args: ["Win32" "VIEW-PRO"]] ; for debugging only
product: to-word any [args/2  "core"]

platform-data: context [type: 'windows]
build: context [features: [help-strings]]

;-- Fetch platform specifications:
;init-build-objects/platform platform
;platform-data: platforms/:platform
;build: platform-data/builds/:product

;-- UTILITIES ----------------------------------------------------------

up-word: func [w] [
	w: uppercase form w
	for-each [f t] [
		#"-" #"_"
	][replace/all w f t]
	w
]

;-- Emit Function
out: make string! 100000
emit: func [data] [repend out data]

to-c-name: func [word] [
	word: form word
	for-each [f t] [
		"..." "ellipsis"
		#"-" #"_"
		#"." #"_"
		#"?" #"q"
		#"!" #"x"
		#"~" ""
		#"*" "_p"
		#"+" "_add"
		#"|" "or_bar"
	][replace/all word f t]
	word
]

emit-enum: func [word] [emit [tab to-c-name word "," newline]]

emit-line: func [prefix word cmt /var /define /code /decl /up1 /local str][

	str: to-c-name word

	if word = 0 [prefix: ""]

	if not any [code decl] [
		either var [uppercase/part str 1] [uppercase str]
	]

	if up1 [uppercase/part str 1]

	str: any [
		if define [rejoin [prefix str]]
		if code   [rejoin ["    " prefix str cmt]]
		if decl   [rejoin [prefix str cmt]]
		rejoin ["    " prefix str ","]
	]
	if any [code decl] [cmt: none]
	if cmt [
		len: 32 - length str
		loop to-integer len / 4 [append str tab]
		any [
			if define [repend str cmt]
			if cmt [repend str ["// " cmt]]
		]
	]
	append str newline
	append out str
]

emit-head: func [title [string!] file [file!]] [
	clear out
	emit form-header/gen title file %make-boot.r
]

emit-end: func [/easy] [
	if not easy [remove find/last out #","]
	append out {^};^/}
]

remove-tests: func [d] [
	while [d: find d #test][remove/part d 2]
]

;----------------------------------------------------------------------------
;
; Evaltypes.h - Evaluation Dispatch Maps
;
;----------------------------------------------------------------------------

boot-types: load %types.r


emit-head "Evaluation Maps" %evaltypes.h


emit {
/***********************************************************************
**
*/	const REBACT Value_Dispatch[REB_MAX] =
/*
**		The ACTION dispatch function for each datatype.
**
***********************************************************************/
^{
}

for-each-record-NO-RETURN type boot-types [
	emit-line/var "T_" type/class type/name
]
emit-end

emit {
/***********************************************************************
**
*/	const REBPEF Path_Dispatch[REB_MAX] =
/*
**		The path evaluator function for each datatype.
**
***********************************************************************/
^{
}

for-each-record-NO-RETURN type boot-types [
	emit-line/var "PD_" switch/default type/path [
		* [type/class]
		- [0]
	][type/path] type/name
]
emit-end

write inc/tmp-evaltypes.h out


;----------------------------------------------------------------------------
;
; Maketypes.h - Dispatchers for Make (used by construct)
;
;----------------------------------------------------------------------------

emit-head "Datatype Makers" %maketypes.h
emit newline

types-used: []

for-each-record-NO-RETURN type boot-types [
	if all [
		type/make = '*
		word? type/class
		not find types-used type/class
	][
		; using -Wredundant-decls it seems these prototypes are already
		; taken care of by make-headers.r, no need to re-emit
		comment [
			emit-line/up1/decl
				"extern REBFLG MT_" type/class "(REBVAL *, REBVAL *, REBCNT);"
		]
		append types-used type/class
	]
]

emit {

/***********************************************************************
**
*/	const MAKE_FUNC Make_Dispatch[REB_MAX] =
/*
**		Specifies the make method used for each datatype.
**
***********************************************************************/
^{
}

for-each-record-NO-RETURN type boot-types [
	either type/make = '* [
		emit-line/var "MT_" type/class type/name
	][
		emit-line "" "0" type/name
	]
]

emit-end

write inc/tmp-maketypes.h out

;----------------------------------------------------------------------------
;
; Comptypes.h - compare functions
;
;----------------------------------------------------------------------------

emit-head "Datatype Comparison Functions" %comptypes.h
emit newline

types-used: []

for-each-record-NO-RETURN type boot-types [
	if all [
		word? type/class
		not find types-used type/class
	][
		; using -Wredundant-decls it seems these prototypes are already
		; taken care of by make-headers.r, no need to re-emit
		comment [
			emit-line/up1/decl
				"extern REBINT CT_" type/class "(REBVAL *, REBVAL *, REBINT);"
		]
		append types-used type/class
	]
]

emit {
/***********************************************************************
**
*/	const REBCTF Compare_Types[REB_MAX] =
/*
**		Type comparision functions.
**
***********************************************************************/
^{
}

for-each-record-NO-RETURN type boot-types [
	emit-line/var "CT_" type/class type/name
]
emit-end

write inc/tmp-comptypes.h out


;----------------------------------------------------------------------------
;
; Moldtypes.h - Dispatchers for Mold and Form
;
;----------------------------------------------------------------------------

;emit-head "Mold Dispatchers"
;
;emit {
;/***********************************************************************
;**
;*/	const MOLD_FUNC Mold_Dispatch[REB_MAX] =
;/*
;**		The MOLD dispatch function for each datatype.
;**
;***********************************************************************/
;^{
;}
;
;for-each-record-NO-RETURN type boot-types [
;	f: "Mold_"
;	switch/default type/mold [
;		* [t: type/class]
;		+ [t: type/name]
;		- [t: 0]
;	][t: uppercase/part form type/mold 1]
;	emit [tab "case " uppercase join "REB_" type/name ":" tab "\\" t]
;	emit newline
;	;emit-line/var f t type/name
;]
;emit-end
;
;emit {
;/***********************************************************************
;**
;*/	const MOLD_FUNC Form_Dispatch[REB_MAX] =
;/*
;**		The FORM dispatch function for each datatype.
;**
;***********************************************************************/
;^{
;}
;for-each-record-NO-RETURN type boot-types [
;	f: "Mold_"
;	switch/default type/form [
;		*  [t: type/class]
;		f* [t: type/class f: "Form_"]
;		+  [t: type/name]
;		f+ [t: type/name f: "Form_"]
;		-  [t: 0]
;	][t: uppercase/part form type/mold 1]
;	emit [tab "case " uppercase join "REB_" type/name ":" tab "\\" t]
;	emit newline
;	;emit-line/var f t type/name
;]
;emit-end
;
;write inc/tmp-moldtypes.h out

;----------------------------------------------------------------------------
;
; Bootdefs.h - Boot include file
;
;----------------------------------------------------------------------------

emit-head "Datatype Definitions" %reb-types.h

emit [
{
/***********************************************************************
**
*/	enum Reb_Kind
/*
**		Internal datatype numbers. These change. Do not export.
**
***********************************************************************/
^{
}
]

datatypes: []
n: 0
for-each-record-NO-RETURN type boot-types [
	append datatypes type/name
	emit-line "REB_" type/name n
	n: n + 1
]
emit {    REB_MAX
^};
}

emit {
/***********************************************************************
**
**	REBOL Type Check Macros
**
***********************************************************************/
}

new-types: []
for-each-record-NO-RETURN type boot-types [
	append new-types to-word join type/name "!"
	str: uppercase form type/name
	replace/all str #"-" #"_"
	def: join {#define IS_} [str "(v)"]
	len: 31 - length def
	loop to-integer len / 4 [append def tab]
	emit [def "(VAL_TYPE(v)==REB_" str ")" newline]
]

emit {
/***********************************************************************
**
**	REBOL Typeset Defines
**
***********************************************************************/
}

typeset-sets: []

for-each-record-NO-RETURN type boot-types [
	for-each ts compose [(type/typesets)] [
		spot: any [
			select typeset-sets ts
			first back insert tail typeset-sets reduce [ts copy []]
		]
		append spot type/name
	]
]
remove/part typeset-sets 2 ; the - markers

for-each [ts types] typeset-sets [
	emit ["#define TS_" up-word ts " ("]
	for-each t types [
		emit ["((REBU64)1<<REB_" up-word t ")|"]
	]
	append remove back tail out ")^/"
]

write-if inc/reb-types.h out

;----------------------------------------------------------------------------
;
; Extension Related Tables
;
;----------------------------------------------------------------------------

ext-types: load %types-ext.r
rxt-record: [type offset size]

; Generate type table with necessary gaps
rxt-types: []
n: 0
for-each :rxt-record ext-types [
	if integer? offset [
		insert/dup tail rxt-types 0 offset - n
		n: offset
	]
	append rxt-types type
	n: n + 1
]


emit-head "Extension Types (Isolators)" %ext-types.h

emit [
{
enum REBOL_Ext_Types
^{
}
]
n: 0
for-each :rxt-record ext-types [
	either integer? offset [
		emit-line "RXT_" rejoin [type " = " offset] n
	][
		emit-line "RXT_" type n
	]
	n: n + 1
]
emit {    RXT_MAX
^};
}

write inc/ext-types.h out ; part of Host-Kit distro

emit-head "Extension Type Equates" %tmp-exttypes.h
emit {

#ifdef __cplusplus
extern "C" ^{
#endif

extern const REBRXT Reb_To_RXT[REB_MAX];

/***********************************************************************
**
*/	const REBRXT Reb_To_RXT[REB_MAX] =
/*
***********************************************************************/
^{
}

for-each-record-NO-RETURN type boot-types [
	either find ext-types type/name [
		emit-line "RXT_" type/name type/name
	][
		emit-line "" 0 type/name
	]
]
emit-end

emit {
/***********************************************************************
**
*/	const REBYTE RXT_To_Reb[RXT_MAX] =
/*
***********************************************************************/
^{
}

n: 0
for-each type rxt-types [
	either word? type [emit-line "REB_" type n][
		emit-line "" 0 n
	]
	n: n + 1
]
emit-end

emit {
/***********************************************************************
**
*/	const REBCNT RXT_Eval_Class[RXT_MAX] =
/*
***********************************************************************/
^{
}

n: 0
for-each type rxt-types [
	either all [
		word? type
		rec: find ext-types type
	][
		emit-line "RXX_" rec/3 rec/1
	][
		emit-line "" 0 n
	]
	n: n + 1
]
emit-end

emit {
#define RXT_ALLOWED_TYPES (}
for-each type next rxt-types [
	if word? type [
		emit replace join "((u64)" uppercase rejoin ["1<<REB_" type ") \^/"] #"-" #"_"
		emit "|"
	]
]
remove back tail out
emit ")^/"

emit {
#ifdef __cplusplus
^}
#endif
}
write inc/tmp-exttypes.h out


;----------------------------------------------------------------------------
;
; Bootdefs.h - Boot include file
;
;----------------------------------------------------------------------------

emit-head "Boot Definitions" %bootdefs.h

emit [
{
#define REBOL_VER } version/1 {
#define REBOL_REV } version/2 {
#define REBOL_UPD } version/3 {
#define REBOL_SYS } version/4 {
#define REBOL_VAR } version/5 {
}
]

;-- Generate Lower-Level String Table ----------------------------------------

emit {
/***********************************************************************
**
**	REBOL Boot Strings
**
**		These are special strings required during boot and other
**		operations. Putting them here hides them from exe hackers.
**		These are all string offsets within a single string.
**
***********************************************************************/
}

boot-strings: load %strings.r
boot-errors: load %errors.r ;-- used also to make %tmp-errnums.r below

code: ""
n: 0
for-each str boot-strings [
	either set-word? :str [
		emit-line/define "#define RS_" to word! str n ;R3
	][
		n: n + 1
		append code str
		append code null
	]
]

; Some errors occur before we have the boot process is far enough along to
; have the error messages loaded in as Rebol strings in the boot block's
; CAT_ERRORS structure.  Adding their strings to %errors.r and keeping them
; in sync with a "pre-boot" copy in %strings.r was a manual process.  This
; makes format strings from the STRING! or BLOCK! in %errors.r automatically.
;
for-each [cat msgs] boot-errors [
	unless cat = quote Internal: [continue]

	emit-line/define "#define RS_" 'ERROR n
	for-each [word val] skip msgs 4 [
		n: n + 1
		case [
			string? val [append code val]
			block? val [
				; %strings.r strings use printf-like convention, %d tells
				; FORM to treat the vararg REBVAL* as an integer.
				;
				append code rejoin map-each item val [
					either get-word? item [{ %v }] [item]
				]
			]
			true [fail {Non-STRING! non-BLOCK! as %errors.r value}]
		]
		append code null
	]
]

emit ["#define RS_MAX" tab n lf]
emit ["#define RS_SIZE" tab length out lf]
boot-strings: to-binary code

;-- Generate Canonical Words (must follow datatypes above!) ------------------

emit {
/***********************************************************************
**
*/	enum REBOL_Symbols
/*
**		REBOL static canonical words (symbols) used with the code.
**
***********************************************************************/
^{
	SYM_0 = 0,
}

n: 1
for-each-record-NO-RETURN type boot-types [
	emit-line "SYM_" join type/name "_type" n
	n: n + 1
]

boot-words: load %words.r

replace boot-words '*port-modes* load %modes.r

for-each word boot-words [
	emit-line "SYM_" word reform [n "-" word]
	n: n + 1
]
emit-end

;-- Generate Action Constants ------------------------------------------------

emit {
/***********************************************************************
**
*/	enum REBOL_Actions
/*
**		REBOL datatype action numbers.
**
***********************************************************************/
^{
}

boot-actions: load %actions.r
n: 1
emit-line "A_" "type = 0" "Handled by interpreter"
for-each word boot-actions [
	if set-word? :word [
		emit-line "A_" to word! :word n ;R3
		n: n + 1
	]
]
emit [tab "A_MAX_ACTION" lf "};"]
emit {

#define IS_BINARY_ACT(a) ((a) <= A_XOR)
}
print [n "actions"]

write inc/tmp-bootdefs.h out

;----------------------------------------------------------------------------
;
; Sysobj.h - System Object Selectors
;
;----------------------------------------------------------------------------

emit-head "System Object" %sysobj.h
emit newline

at-value: func ['field] [next find boot-sysobj to-set-word field]

boot-sysobj: load %sysobj.r
change at-value version version
when: now
when: when - when/zone
when/zone: 0:00
change at-value build when
change at-value product to lit-word! product


plats: load %platforms.r

change/only at-value platform reduce [
	any [select plats version/4 "Unknown"]
	any [select third any [find/skip plats version/4 3 []] version/5 ""]
]

ob: context boot-sysobj

make-obj-defs: func [obj prefix depth /local f] [
	uppercase prefix
	emit ["enum " prefix "object {" newline]
	emit-line prefix "SELF = 0" none
	for-each field words-of obj [ ;R3
		emit-line prefix field none
	]
	emit [tab uppercase join prefix "MAX^/"]
	emit "};^/^/"

	if depth > 1 [
		for-each field words-of obj [ ;R3
			f: join prefix [field #"_"]
			replace/all f "-" "_"
			all [
				field <> 'standard
				object? get in obj field
				make-obj-defs obj/:field f depth - 1
			]
		]
	]
]

make-obj-defs ob "SYS_" 1
make-obj-defs ob/catalog "CAT_" 4
make-obj-defs ob/contexts "CTX_" 4
make-obj-defs ob/standard "STD_" 4
make-obj-defs ob/state "STATE_" 4
;make-obj-defs ob/network "NET_" 4
make-obj-defs ob/ports "PORTS_" 4
make-obj-defs ob/options "OPTIONS_" 4
;make-obj-defs ob/intrinsic "INTRINSIC_" 4
make-obj-defs ob/locale "LOCALE_" 4
make-obj-defs ob/view "VIEW_" 4

write inc/tmp-sysobj.h out

;----------------------------------------------------------------------------

emit-head "Dialects" %reb-dialect.h
emit {
enum REBOL_dialect_error {
	REB_DIALECT_END = 0,	// End of dialect block
	REB_DIALECT_MISSING,	// Requested dialect is missing or not valid
	REB_DIALECT_NO_CMD,		// Command needed before the arguments
	REB_DIALECT_BAD_SPEC,	// Dialect spec is not valid
	REB_DIALECT_BAD_ARG,	// The argument type does not match the dialect
	REB_DIALECT_EXTRA_ARG	// There are more args than the command needs
};

}
make-obj-defs ob/dialects "DIALECTS_" 4

emit {#define DIALECT_LIT_CMD 0x1000
}

write inc/reb-dialect.h out


;----------------------------------------------------------------------------
;
; Event Types
;
;----------------------------------------------------------------------------

emit-head "Event Types" %reb-evtypes.h
emit newline

emit ["enum event_types {" newline]
for-each field ob/view/event-types [
	emit-line "EVT_" field none
]
emit [tab "EVT_MAX^/"]
emit "};^/^/"

emit ["enum event_keys {" newline]
emit-line "EVK_" "NONE" none
for-each field ob/view/event-keys [
	emit-line "EVK_" field none
]
emit [tab "EVK_MAX^/"]
emit "};^/^/"

write inc/reb-evtypes.h out


;----------------------------------------------------------------------------
;
; Error Constants
;
;----------------------------------------------------------------------------

;-- Error Structure ----------------------------------------------------------

emit-head "Error Structure and Constants" %errnums.h

emit {
#ifdef VAL_TYPE
/***********************************************************************
**
*/	typedef struct REBOL_Error_Obj
/*
***********************************************************************/
^{
}
; Generate ERROR object and append it to bootdefs.h:
emit-line/code "REBVAL " 'self ";" ;R3
for-each word words-of ob/standard/error [ ;R3
	if word = 'near [word: 'nearest] ; prevents C problem
	emit-line/code "REBVAL " word ";"
]
emit {^} ERROR_OBJ;
#endif
}

emit {
/***********************************************************************
**
*/	enum REBOL_Errors
/*
***********************************************************************/
^{
}

err-list: make block! 200
errs: false

for-each [cat msgs] boot-errors [
	code: second msgs
	new1: true
	for-each [word val] skip msgs 4 [
		err: uppercase form to word! word ;R3
		replace/all err "-" "_"
		if find err-list err [print ["DUPLICATE Error Constant:" err] errs: true]
		append err-list err
		either new1 [
			emit-line "RE_" reform [err "=" code] reform [code mold val]
			new1: false
		][
			emit-line "RE_" err reform [code mold val]
		]
		code: code + 1
	]
	emit-line "RE_" join to word! cat "_max" none ;R3
	emit newline
]

if errs [fail "Invalid errors.r input"]

emit-end

emit {
#define RE_INTERNAL_FIRST RE_MISC // update if no longer first
#define RE_USER RE_MESSAGE
#define RE_MAX RE_USER_MAX // update if another category added
}

write inc/tmp-errnums.h out

;-------------------------------------------------------------------------

emit-head "Port Modes" %port-modes.h

data: load %modes.r

emit {
enum port_modes ^{
}

for-each word data [
	emit-enum word
]
emit-end

write inc/tmp-portmodes.h out

;----------------------------------------------------------------------------
;
; Load Boot Mezzanine Functions - Base, Sys, and Plus
;
;----------------------------------------------------------------------------

;-- Add other MEZZ functions:
mezz-files: load %../mezz/boot-files.r ; base lib, sys, mezz

;append boot-mezz+ none ?? why was this needed?

for-each section [boot-base boot-sys boot-mezz] [
	set section make block! 200
	for-each file first mezz-files [
		append get section load join %../mezz/ file
	]
	remove-tests get section

	;-- Expectation is that section does not return result; PAREN! makes unset
	append get section [()]

	mezz-files: next mezz-files
]

boot-protocols: make block! 20
for-each file first mezz-files [
	m: load/all join %../mezz/ file ; not REBOL word
	append/only append/only boot-protocols m/2 skip m 2
]

emit-head "Sys Context" %sysctx.h
sctx: construct boot-sys
make-obj-defs sctx "SYS_CTX_" 1
write inc/tmp-sysctx.h out


;----------------------------------------------------------------------------
;
; b-boot.c - Boot data file
;
;----------------------------------------------------------------------------

;-- Build b-boot.c output file -------------------------------------------------


emit-head "Natives and Bootstrap" %b-boot.c
emit {
#include "sys-core.h"

}

externs: make string! 2000
boot-booters: load %booters.r
boot-natives: load %natives.r

nats: append copy boot-booters boot-natives

n: boot-sys
;while [n: find n 'native] [
;	if set-word? first back n [
;		print index-of n
;		append nats copy/part back n 3
;	]
;	n: next next n
;]

nat-count: 0

for-each val nats [
	if set-word? val [
		emit-line/decl "REBNATIVE(" to word! val ");" ;R3
		nat-count: nat-count + 1
	]
]

print [nat-count "natives"]

emit [newline {const REBFUN Native_Funcs[} nat-count {] = ^{
}]
for-each val nats [
	if set-word? val [
		emit-line/code "N_" to word! val "," ;R3
	]
	;nat-count: nat-count + 1
]
emit-end
emit newline

;-- Embedded REBOL Tests:
;where: find boot/script 'tests
;if where [
;	remove where
;	for-each file sort load %../tests/ [
;		test: load join %../tests/ file
;		if test/1 <> 'skip-test [
;			where: insert where test
;		]
;	]
;]

;-- Build typespecs block (in same order as datatypes table):
boot-typespecs: make block! 100
specs: load %typespec.r
for-each type datatypes [
	append/only boot-typespecs select specs type
]

;-- Create main code section (compressed):
boot-types: new-types
boot-root: load %root.r
boot-task: load %task.r
boot-ops: load %ops.r
;boot-script: load %script.r

write %boot-code.r mold reduce sections
data: mold/flat reduce sections
insert data reduce ["; Copyright (C) REBOL Technologies " now newline]
insert tail data make char! 0 ; scanner requires zero termination

comp-data: compress data: to-binary data

emit [
{
// Native_Specs contains data which is the DEFLATE-algorithm-compressed
// representation of the textual function specs for Rebol's native
// routines.  Though DEFLATE includes the compressed size in the payload,
// NAT_UNCOMPRESSED_SIZE is also defined to be used as a sanity check
// on the decompression process.
}
newline
]

emit ["const REBYTE Native_Specs[NAT_COMPRESSED_SIZE] = {" newline]

;-- Convert UTF-8 binary to C-encoded string:
emit binary-to-c comp-data
emit-end/easy

write src/b-boot.c out

;-- Output stats:
print [
	"Compressed" length data "to" length comp-data "bytes:"
	to-integer ((length comp-data) / (length data) * 100)
	"percent of original"
]

;-- Create platform string:
;platform: to-string platform
;lowercase platform
;if platform-data/type = 'windows [ ; Why?? Not sure.
;	product: to-string product
;	lowercase product
;	replace/all product "-" ""
;]
;;dir: to-file rejoin [%../to- platform "/" product "/temp/"]

;----------------------------------------------------------------------------
;
; Boot.h - Boot header file
;
;----------------------------------------------------------------------------

emit-head "Bootstrap Structure and Root Module" %boot.h

emit [
{
#define MAX_NATS      } nat-count {
#define NAT_UNCOMPRESSED_SIZE } length data {
#define NAT_COMPRESSED_SIZE } length comp-data {
#define CHECK_TITLE   } checksum to binary! title {

extern const REBYTE Native_Specs[];
extern const REBFUN Native_Funcs[];

typedef struct REBOL_Boot_Block ^{
}
]

for-each word sections [
	word: form word
	remove/part word 5 ; boot_
	emit-line/code "REBVAL " word ";"
]
emit "} BOOT_BLK;"

;-------------------

emit [
{

//**** ROOT Context (Root Module):

typedef struct REBOL_Root_Context ^{
}
]

for-each word boot-root [
	emit-line/code "REBVAL " word ";"
]
emit ["} ROOT_CTX;" lf lf]

n: 0
for-each word boot-root [
	emit-line/define "#define ROOT_" word join "(&Root_Context->" [lowercase replace/all form word #"-" #"_" ")"]
	n: n + 1
]
emit ["#define ROOT_MAX " n lf]

;-------------------

emit [
{

//**** Task Context

typedef struct REBOL_Task_Context ^{
}
]

for-each word boot-task [
	emit-line/code "REBVAL " word ";"
]
emit ["} TASK_CTX;" lf lf]

n: 0
for-each word boot-task [
	emit-line/define "#define TASK_" word join "(&Task_Context->" [lowercase replace/all form word #"-" #"_" ")"]
	n: n + 1
]
emit ["#define TASK_MAX " n lf]

write inc/tmp-boot.h out
;print ask "-DONE-"
;wait .3
print "   "
