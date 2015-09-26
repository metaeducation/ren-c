REBOL [
	System: "REBOL [R3] Language Interpreter and Run-time Environment"
	Title: "Make cmake files for libffi"
	Rights: {
		Copyright 2012 REBOL Technologies
		REBOL is a trademark of REBOL Technologies
	}
	License: {
		Licensed under the Apache License, Version 2.0
		See: http://www.apache.org/licenses/LICENSE-2.0
	}
	Author: "Shixin Zeng"
	Purpose: {
		Build cmake files for libffi
	}
	Note: [
		"This runs relative to ../tools directory."
		"Make OS-specific changes to the systems.r file."
	]
]

libffi: %../../src/libffi

do %common.r
do %systems.r

opts: system/options/args

; format: make-cmake.r outdir os-id compiler
out-dir: to file! take opts
os-id: opts
if block? opts [
	os-id: first opts
]
;print ["os-id:" mold os-id]
;print ["opts:" mold opts]

unless #"/" = first out-dir [
	out-dir: join system/options/path out-dir
]

config: config-system/guess os-id

print ["Option set for building:" config/id config/os-name]

either all [block? opts 1 < length opts][
	compiler-name: to-word second opts
][
	compiler-name: first fourth find systems config/id
]
compiler-spec: select fourth (find systems config/id) compiler-name
if none? compiler-spec [
	do make error! rejoin [
		"Compiler" compiler-name " is not supported" newline
		"Supported compilers are: " mold extract (fourth (find systems config/id)) 2 newline
	]
]

compiler-obj: make object! compiler-spec
if found? find words-of compiler-obj 'FFI [
	compiler-obj/FFI: make object! compiler-obj/FFI
]

output: copy ""
emit: func [d] [repend output d]

foreach [n v] compiler-obj/FFI/PREDEFINES [
	emit [ {#define } to word! n space mold v newline]
]

emit [ 
{
#ifdef HAVE_HIDDEN_VISIBILITY_ATTRIBUTE
#ifdef LIBFFI_ASM
#define FFI_HIDDEN(name) .hidden name
#else
#define FFI_HIDDEN __attribute__ ((visibility ("hidden")))
#endif
#else
#ifdef LIBFFI_ASM
#define FFI_HIDDEN(name)
#else
#define FFI_HIDDEN
#endif
#endif
}
]

include-dir: join out-dir %/include/
unless exists? include-dir [
	mkdir/deep include-dir
]
write join include-dir %fficonfig.h output

lines: copy []
foreach line read/lines libffi/include/ffi.h.in [
	if parse line [thru "@" copy var to "@" to end][
		;print ["var:" mold var]
		either val: compiler-obj/FFI/PREDEFINES/(to set-word! var) [
			replace line rejoin ["@" var "@"] val
		][
			replace line rejoin ["@" var "@"] 0
		]
	]
	append lines line
]

write/lines join include-dir %ffi.h lines

write join include-dir %ffitarget.h read libffi/src/(compiler-obj/FFI/TARGET-DIRECTORY)/ffitarget.h
