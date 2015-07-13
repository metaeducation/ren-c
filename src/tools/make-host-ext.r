REBOL [
	System: "REBOL [R3] Language Interpreter and Run-time Environment"
	Title: "Build REBOL 3.0 boot extension module"
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
	Purpose: {
		Collects host-kit extension modules and writes them out
		to a .h file in a compilable data format.
	}
]

print "--- Make Host Boot Extension ---"

do %common.r

secure none
do %form-header.r

;-- Collect Sources ----------------------------------------------------------

collect-files: func [
	"Collect contents of several source files and return combined with header."
	files [block!]
	/local source data header
][
	source: make block! 1000

	foreach file files [
		data: load/all file
		remove-each [a b] data [issue? a] ; commented sections
		unless block? header: find data 'rebol [
			print ["Missing header in:" file] halt
		]
		unless empty? source [data: next next data] ; first one includes header
		append source data
	]

	source
]

;-- Emit Functions -----------------------------------------------------------

out: make string! 10000
emit: func [d] [repend out d]

emit-cmt: func [text] [
	emit [
{/***********************************************************************
**
**  } text {
**
***********************************************************************/

}]
]

form-name: func [word] [
	uppercase replace/all replace/all to-string word #"-" #"_" #"?" #"Q"
]

emit-file: func [
	"Emit command enum and source script code."
	file [file!]
	source [block!]
	/local title name data exports words exported-words src prefix
][
	source: collect-files source

	title: select source/2 to-set-word 'title
	name: form select source/2 to-set-word 'name
	replace/all name "-" "_"
	prefix: uppercase copy name

	clear out
	emit form-header/gen title second split-path file %make-host-ext.r

	emit ["enum " name "_commands {^/"]

	; Gather exported words if exports field is a block:
	words: make block! 100
	exported-words: make block! 100
	src: source
	while [src: find src set-word!] [
		if all [
			<no-export> <> first back src
			find [command func function funct] src/2
		][
			append exported-words to-word src/1
		]
		if src/2 = 'command [append words to-word src/1]
		src: next src
	]

	if block? exports: select second source to-set-word 'exports [
		insert exports exported-words
	]

	foreach word words [emit [tab "CMD_" prefix #"_" replace/all form-name word "'" "_LIT"  ",^/"]]
	emit [tab "CMD_MAX" newline]
	emit "};^/^/"

	if src: select source to-set-word 'words [
		emit ["enum " name "_words {^/"]
		emit [tab "W_" prefix "_0,^/"]
		foreach word src [emit [tab "W_" prefix #"_" form-name word ",^/"]]
		emit [tab "W_MAX" newline]
		emit "};^/^/"
	]

	emit "#ifdef INCLUDE_EXT_DATA^/"
	data: append trim/head mold/only/flat source newline
	append data to-char 0 ; null terminator may be required
	emit ["const unsigned char RX_" name "[] = {^/" binary-to-c data "};^/^/"]
	emit "#endif^/"

	write rejoin [%../include/ file %.h] out

;	clear out
;	emit form-header/gen join title " - Module Initialization" second split-path file %make-host-ext.r
;	write rejoin [%../os/ file %.c] out
]
