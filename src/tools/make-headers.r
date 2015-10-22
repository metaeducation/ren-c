REBOL [
	System: "REBOL [R3] Language Interpreter and Run-time Environment"
	Title: "Generate auto headers"
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

do %common.r

print "------ Building headers"

r3: system/version > 2.100.0

verbose: false
check-duplicates: true
prototypes: make block! 10000 ; get pick [map! hash!] r3 1000
has-duplicates: false

do %form-header.r

change-dir %../core/

emit-out: func [d] [append repend output-buffer d newline]
emit-rlib: func [d] [append repend rlib d newline]
emit-header: func [t f] [emit-out form-header/gen t f %make-headers]

emit-proto: func [proto] [

	if find proto "()" [
		print [
			proto
			newline
			{C-Style void arguments should be foo(void) and not foo()}
			newline
			http://stackoverflow.com/questions/693788/c-void-arguments
		]
		fail "C++ no-arg prototype used instead of C style"
	]

	;?? proto
	assert [proto]
	if all [
		not find proto "static"
		not find proto "REBNATIVE("

		; The REBTYPE macro actually is expanded in the tmp-funcs
		; Should we allow macro expansion or do the REBTYPE another way?
		(comment [not find proto "REBTYPE("] true)

		find proto #"("
	][
		proto: trim proto
		either all [
			check-duplicates
			find prototypes proto
		][
			print ["Duplicate:" the-file ":" proto]
			has-duplicates: true
		][
			append prototypes proto
		]
		either find proto "RL_API" [
			emit-rlib ["extern " proto "; // " the-file]
		][
			emit-out ["extern " proto "; // " the-file]
		]
		proto-count: proto-count + 1
	]
]

func-header: [
	;-- Scan for function header box:
	"^/**" to newline
	"^/*/" any [#" " | #"^-"]
	copy proto to newline (emit-proto proto) newline
	opt [
		"/*" ; must be in func header section, not file banner
		any [
			thru "**" [#" " | #"^-"] copy line thru newline
		]
		thru "*/"
	]
]

segment: [
	thru "/******" to newline [
		func-header
		| thru newline
	]
]

process: func [file] [
	if verbose [?? file]
	data: read the-file: file
	if r3 [data: deline to-string data]
	parse data [any segment]
]

;-------------------------------------------------------------------------

rlib: form-header/gen "REBOL Interface Library" %reb-lib.h %make-headers.r
append rlib newline


;-------------------------------------------------------------------------

proto-count: 0
output-buffer: make string! 20000

emit-header "Function Prototypes" %funcs.h

emit-out {
#ifdef __cplusplus
extern "C" ^{
#endif
}

files: sort read %./

;do
[
	remove find files %a-lib2.c
	print "Non-extended reb-lib version"
	wait 5
]

for-each file files [
	if all [
		%.c = suffix? file
		not find/match file "host-"
		not find/match file "os-"
	][process file]
]

emit-out {
#ifdef __cplusplus
^}
#endif
}

write %../include/tmp-funcs.h output-buffer

print [proto-count "function prototypes"]
;wait 1

;-------------------------------------------------------------------------

clear output-buffer

emit-header "Function Argument Enums" %func-args.h

make-arg-enums: func [word] [
	; Search file for definition:
	def: find action-list to-set-word word
	def: skip def 2
	args: copy []
	refs: copy []
	; Gather arg words:
	for-each w first def [
		if any-word? w [
			append args uw: uppercase replace/all form to word! w #"-" #"_" ; R3
			if refinement? w [append refs uw  w: to word! w] ; R3
	 	]
	]

	uword: uppercase form word
	replace/all uword #"-" #"_"
	word: lowercase copy uword

	; Argument numbers:
	emit-out ["enum act_" word "_arg {"]
	emit-out [tab "ARG_" uword "_0,"]
	for-each w args [emit-out [tab "ARG_" uword "_" w ","]]
	emit-out [tab "ARG_" uword "_MAX"]
	emit-out "};^/"

	; Argument bitmask:
	n: 0
	emit-out ["enum act_" word "_mask {"]
	for-each w args [
		emit-out [tab "AM_" uword "_" w " = 1 << " n ","]
		n: n + 1
	]
	emit-out [tab "AM_" uword "_MAX"]
	emit-out "};^/"

	repend output-buffer ["#define ALL_" uword "_REFS ("]
	for-each w refs [
		repend output-buffer ["AM_" uword "_" w "|"]
	]
	remove back tail output-buffer
	append output-buffer ")^/^/"

	;?? output-buffer halt
]

action-list: load %../boot/actions.r

for-each word [
	copy
	find
	select
	insert
	trim
	open
	read
	write
] [make-arg-enums word]

action-list: load %../boot/natives.r

for-each word [
	checksum
	request-file
] [make-arg-enums word]

;?? output-buffer
write %../include/tmp-funcargs.h output-buffer


;-------------------------------------------------------------------------

clear output-buffer

emit-header "REBOL Constants Strings" %str-consts.h

data: to string! read %a-constants.c ;R3

parse data [
	some [
		to "^/const"
		copy constd to "="
		(
			remove constd
			;replace constd "const" "extern"
			insert constd "extern "
			append trim/tail constd #";"
			emit-out constd
		)
	]
]

write %../include/tmp-strings.h output-buffer

if any [has-duplicates verbose] [
	print "** NOTE ABOVE PROBLEM!"
	wait 5
]

print "   "
