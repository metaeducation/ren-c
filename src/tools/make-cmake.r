REBOL [
	System: "REBOL [R3] Language Interpreter and Run-time Environment"
	Title: "Make cmake files for R3"
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
		Build cmake files for a given platform and compiler.
	}
	Note: [
		"This file is based on make-make.r"

		"This runs relative to ../tools directory."
		"Make OS-specific changes to the systems.r file."
	]
]

path-make:   %../../make/

;******************************************************************************

; (Warning: format is a bit sensitive to extra spacing. E.g. see macro+ func)

makefile-head:

{# REBOL Makefile -- Generated by make-make.r (!!! EDITS WILL BE LOST !!!)
# This automatically produced file was created !date

cmake_minimum_required (VERSION 2.6)
project (Rebol3 C ASM)
set (TOP_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../src")
set (CORE_DIR "${TOP_SRC_DIR}/core")
set (OS_DIR "${TOP_SRC_DIR}/os")
set (FFI_DIR "${TOP_SRC_DIR}/libffi")
set (TOOLS_DIR "${TOP_SRC_DIR}/tools")
}

;******************************************************************************
;** Options and Config
;******************************************************************************

file-base: make object! load %file-base.r

do %common.r
do %systems.r

opts: system/options/args

; format: make-cmake.r os-id compiler
os-id: opts
if block? opts [
	os-id: first opts
]

config: config-system/guess system/options/args

print ["Option set for building:" config/id config/os-name]

either all [block? opts 1 < length? opts][
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
build-FFI?: no
if found? find words-of compiler-obj 'FFI [
	compiler-obj/FFI: make object! compiler-obj/FFI
	build-FFI?: yes
]
print ["Compiler:" compiler-name mold compiler-obj]

; Words are cleaner-looking in the table, and hyphens look better (and are
; easier to type).  But we need a string, and one that C can accept and not
; think you're doing subtraction.  Transform it (e.g. osx-64 => "TO_OSX_X64")
to-base-def: rejoin [{TO_} uppercase to-string config/os-base]
to-name-def: rejoin [
	{TO_} replace/all (uppercase to-string config/os-name) {-} {_}
]

; Make plat id string:
plat-id: form config/id/2
if tail? next plat-id [insert plat-id #"0"]
append plat-id config/id/3

; Collect OS-specific host files:
unless os-specific-objs: select file-base to word! join "os-" config/os-base [
	do make error! rejoin [
		"make-make.r requires os-specific obj list in file-base.r" newline
		"none was provided for os-" config/os-base
	]
]

; The + sign is used to tell the make-os-ext.r script to scan a host kit file
; for headers (the way make-headers.r does).  But we don't care about that
; here in make-make.r... so remove any + signs we find before processing.

remove-each item file-base/os [item = '+]
remove-each item os-specific-objs [item = '+]

output: make string! 10000

;******************************************************************************
;** Functions
;******************************************************************************

flag?: func [flags 'word] [found? find flags word]

macro: func [flags obj [object!] /local out] [
	out: make string! 10
	foreach n words-of obj [
		all [
			obj/:n
			flag? flags (n)
			repend out [space obj/:n]
		]
	]
	out
]

emit: func [d] [repend output d]

emit-src-files: func [
	"Output a line-wrapped list of object files."
	dir [any-string!]
	files [block!]
	/abs
	/local cnt
][
	emit ["set (" uppercase dir "_SOURCE " newline]
	foreach file files [
		either abs [
			emit [tab file newline]
		][
			emit [tab "${" uppercase dir "_DIR}/" file newline]
		]
	]
	emit [")" newline]
]

process-tools: func [
	tools [block!]
	/local generated-files tool files args file arg
][
	generated-files: copy []
	foreach [tool files args] tools [
		if not empty? files [
			emit [ {add_custom_command(OUTPUT} newline]
			foreach file files [
				append generated-files file
				emit [tab to string! file newline]
			]

			emit [tab "COMMAND ${REBOL} ${TOOLS_DIR}/" tool ".r"]
			foreach arg args [
				emit [" " arg]
			]

			emit [newline]
			emit [")" newline]
		]
	]
	generated-files
]

;******************************************************************************
;** Build
;******************************************************************************

replace makefile-head "!date" now

if flag? compiler-obj/OTHFLAGS +SC [remove find os-specific-objs 'host-readline.c]

emit makefile-head

;print ["config:" mold config]
emit [ "if(CMAKE_HOST_WIN32)" newline]
emit [ "set (REBOL ^"${CMAKE_CURRENT_SOURCE_DIR}/r3-make.exe^")^/" ]
emit [ "else()" newline]
emit [ "set (REBOL ^"${CMAKE_CURRENT_SOURCE_DIR}/r3-make^")^/" ]
emit [ "endif()" newline]

generated-ffi-files: copy []
;*** FFI
if build-FFI? [
	emit [ "#FFI" newline]
	emit [ "set(FFI_SOURCE " newline]
	foreach s join file-base/ffi-files compiler-obj/FFI/SOURCE [
		emit [ tab "${FFI_DIR}/" s newline]
	]
	emit [")" newline newline]

	emit ["set (FFI_TARGET " mold lowercase compiler-obj/FFI/PREDEFINES/(to set-word! 'TARGET) ")" newline]

	ffi-tools: compose/deep [
		make-libffi [
			"${FFI_DIR}/include/fficonfig.h"
			"${FFI_DIR}/include/ffi.h"
			"${FFI_DIR}/include/ffitarget.h"
		] [(config/id) (compiler-name)]
	]

	generated-ffi-files: process-tools ffi-tools
	if compiler-name = 'MSVC [
		emit [ "enable_language(ASM_MASM)^/"]
		either compiler-obj/FFI/PREDEFINES/TARGET = "X86_WIN64" [
			processer-bits: "64"
		][
			processer-bits: "32"
		]
		asm-file: rejoin ["win" processer-bits "_plain.asm"]
		emit [ {add_custom_command(OUTPUT
	$^{CMAKE_CURRENT_BINARY_DIR^}/} asm-file {
	COMMAND cl.exe /EP /P /I . /I ${FFI_DIR}/x86 /I $^{FFI_DIR^}/include $^{FFI_DIR^}/src/x86/win} processer-bits {.S /Fi} asm-file {)} newline]
		emit [ {set_source_files_properties($^{CMAKE_CURRENT_BINARY_DIR^}/} asm-file { PROPERTIES COMPILE_FLAGS "/Cx /c /coff"
	GENERATED true)} newline]
		append generated-ffi-files join "${CMAKE_CURRENT_BINARY_DIR}/" asm-file
	]
	emit-src-files/abs "generated_ffi" generated-ffi-files

	emit [ "add_library(ffi OBJECT ${FFI_SOURCE} ${GENERATED_FFI_SOURCE})" newline]
	emit [ "target_compile_definitions(ffi PUBLIC HAVE_CONFIG_H)" newline]
	unless empty? compiler-obj/FFI/CFLAGS [
		emit [ "set_target_properties(ffi PROPERTIES COMPILE_FLAGS " macro compiler-obj/FFI/CFLAGS compiler-flags ")" newline]
	]
	emit [ "target_include_directories(ffi PUBLIC" newline]
	foreach i compiler-obj/FFI/INCLUDES [
		emit [ tab "${FFI_DIR}/" i newline]
	]
	emit [ ")" newline]
]
;********************************************************************************

core-tools: compose/deep [
	make-headers [
			"${TOP_SRC_DIR}/include/tmp-funcs.h"
			"${TOP_SRC_DIR}/include/tmp-funcargs.h"
			"${TOP_SRC_DIR}/include/tmp-strings.h"
		] [ ]

	make-boot [
		"${TOP_SRC_DIR}/include/tmp-evaltypes.h"
		"${TOP_SRC_DIR}/include/tmp-maketypes.h"
		"${TOP_SRC_DIR}/include/tmp-comptypes.h"
		"${TOP_SRC_DIR}/include/reb-types.h"
		"${TOP_SRC_DIR}/include/ext-types.h"
		"${TOP_SRC_DIR}/include/tmp-exttypes.h"
		"${TOP_SRC_DIR}/include/tmp-bootdefs.h"
		"${TOP_SRC_DIR}/include/tmp-sysobj.h"
		"${TOP_SRC_DIR}/include/reb-dialect.h"
		"${TOP_SRC_DIR}/include/reb-evtypes.h"
		"${TOP_SRC_DIR}/include/tmp-errnums.h"
		"${TOP_SRC_DIR}/include/tmp-portmodes.h"
		"${TOP_SRC_DIR}/include/tmp-sysctx.h"
		"${TOP_SRC_DIR}/include/tmp-boot.h"
		"${TOP_SRC_DIR}/core/b-boot.c"
	] [(config/id)]

	core-ext [
		"${TOP_SRC_DIR}/include/host-ext-core.h"
	] [ ]

	make-reb-lib [
		"${TOP_SRC_DIR}/include/reb-lib.h"
		"${TOP_SRC_DIR}/include/reb-lib-lib.h"
	] [ ]

	make-os-ext [
		"${TOP_SRC_DIR}/include/host-lib.h" ;sys-core.h requires this
		"${TOP_SRC_DIR}/include/host-table.inc"
	] [ (config/id) ]
]

os-tools: [
	make-host-init [
	] [ ]
]
emit ["#CORE" newline]
emit-src-files "core" file-base/core
generated-core-files: process-tools core-tools
emit-src-files/abs "generated_core" generated-core-files

emit ["#HOST" newline]
emit-src-files "os" append copy file-base/os os-specific-objs
generated-os-files: process-tools os-tools
emit-src-files/abs "generated_os" generated-os-files

emit [ {
if(NOT (MSVC_IDE OR XCODE))
	add_custom_target(clean-generated COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/clean-generated.cmake)
	add_custom_target(clean-all
	   COMMAND ${CMAKE_BUILD_TOOL} clean
	   COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/clean-generated.cmake
	)
endif()
}
]

emit ["add_library(r3_core OBJECT ${CORE_SOURCE} ${GENERATED_CORE_SOURCE})" newline]
emit ["add_library(r3_os OBJECT ${OS_SOURCE} ${GENERATED_OS_SOURCE})" newline]
emit ["add_dependencies(r3_os r3_core)" newline]
either build-FFI? [
	emit ["add_executable(r3 $<TARGET_OBJECTS:r3_core> $<TARGET_OBJECTS:r3_os> $<TARGET_OBJECTS:ffi>)" newline]
][
	emit ["add_executable(r3 $<TARGET_OBJECTS:r3_core> $<TARGET_OBJECTS:r3_os>)" newline]
]
emit ["SET_TARGET_PROPERTIES(r3 PROPERTIES LINKER_LANGUAGE C)" newline]
emit ["set(COMMON_MACROS " to-base-def space to-name-def macro compiler-obj/PREDEFINES compiler-flags ")" newline]

unless empty? macro compiler-obj/OPTFLAGS compiler-flags [
	emit [
		{if(NOT CMAKE_BUILD_TYPE MATCHES "[Dd][Ee][Bb][Uu][Gg]")} newline
			tab "set(CMAKE_C_FLAGS ^"${CMAKE_C_FLAGS}" macro compiler-obj/OPTFLAGS compiler-flags "^")" newline
		"endif()"
		newline
	]
]

either build-FFI? [
	emit ["target_include_directories(r3_core PUBLIC ${TOP_SRC_DIR}/include ${TOP_SRC_DIR}/codecs ${TOP_SRC_DIR}/libffi/include)" newline]
][
	emit ["target_include_directories(r3_core PUBLIC ${TOP_SRC_DIR}/include ${TOP_SRC_DIR}/codecs)" newline]
]
emit ["target_include_directories(r3_os PUBLIC ${TOP_SRC_DIR}/include ${TOP_SRC_DIR}/codecs)" newline]
either build-FFI? [
	emit ["target_compile_definitions(r3_core PUBLIC REB_API HAVE_LIBFFI_AVAILABLE ${COMMON_MACROS})" newline]
][
	emit ["target_compile_definitions(r3_core PUBLIC REB_API ${COMMON_MACROS})" newline]
]
emit ["target_compile_definitions(r3_os PUBLIC REB_CORE REB_EXE ${COMMON_MACROS})" newline]
if found? find words-of compiler-obj 'DBGFLAGS [
	emit [ {set(CMAKE_C_FLAGS_DEBUG} macro compiler-obj/DBGFLAGS compiler-flags {)} newline]
]
emit ["set(CMAKE_C_FLAGS ^"${CMAKE_C_FLAGS}" macro compiler-obj/CFLAGS compiler-flags "^")" newline]
emit ["set(CMAKE_EXE_LINKER_FLAGS ^"${CMAKE_EXE_LINKER_FLAGS}" macro compiler-obj/LDFLAGS linker-flags "^")" newline]
emit ["target_link_libraries(r3" macro compiler-obj/LIBS linker-flags 
		either build-FFI? [ macro compiler-obj/FFI/LIBS linker-flags ][""]
		")" newline]

write path-make/%CMakeLists.txt output

;;;clean-generated.cmake
clean-generated: copy {set(generated }
foreach file join generated-core-files join generated-os-files generated-ffi-files [
	append clean-generated join to string! file newline
]
append clean-generated 
{
	)
foreach (file ${generated})
	if(EXISTS ${file})
		file(REMOVE ${file})
	endif()
endforeach(file)
}

write %../../make/clean-generated.cmake clean-generated
