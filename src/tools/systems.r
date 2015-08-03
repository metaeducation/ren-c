REBOL [
	System: "REBOL [R3] Language Interpreter and Run-time Environment"
	Title: "System build targets"
	Rights: {
		Copyright 2012 REBOL Technologies
		REBOL is a trademark of REBOL Technologies
	}
	License: {
		Licensed under the Apache License, Version 2.0
		See: http://www.apache.org/licenses/LICENSE-2.0
	}
	Author: ["Carl Sassenrath" "@HostileFork" "contributors"]
	Purpose: {
		These are the target system definitions used to build REBOL
		with a variety of compilers and libraries.  We prefer to keep it
		simple like this rather than using a complex configuration tool
		that could make it difficult to support REBOL on older platforms.

		Note that these numbers for the OS are the minor numbers at the
		tail of the system/version tuple.  (The first tuple values are
		used for the Rebol code version itself.)

		If you have a comment to make about a build, make it in the
		form of a flag...even if the functionality for that flag is a no-op.
		This keeps the table definition clean and readable.

		This file uses a table format processed by routines in %common.r,
		so be sure to include that via DO before calling CONFIG-SYSTEM.
	}
]

systems: [
	;-------------------------------------------------------------------------
	[id			os-name			os-base
			compilers]
	;-------------------------------------------------------------------------
	0.1.03		amiga			posix
			[
				GCC [
						PREDEFINES: [BEN LLC]
						CFLAGS: [HID NPS]
						OPTFLAGS: []
						DBGFLAGS: [+g3]
						LIBS: [-LM]
						LDFLAGS: []
						OTHFLAGS: [+SC CMT COP -SP]
				]
			]

	;-------------------------------------------------------------------------
	0.2.04		osx-ppc			osx
			[
				GCC [
						PREDEFINES: [BEN LLC]
						CFLAGS: [NCM]
						OPTFLAGS: [+OS]
						DBGFLAGS: [+g3]
						LIBS: [-LM]
						LDFLAGS: [NSO]
						OTHFLAGS: []
				]
			]

	0.2.05		osx-x86			osx
			[
				GCC [
						PREDEFINES: [LEN LLC]
						CFLAGS: [ARC NPS PIC NCM HID]
						OPTFLAGS: [+O1]
						DBGFLAGS: [+g3]
						LIBS: [-LM]
						LDFLAGS: []
						OTHFLAGS: [STX]
				]
			]

	0.2.40		osx-x64			osx
			[
				GCC [
						PREDEFINES: [LP64 LEN LLC]
						CFLAGS: [NPS PIC NCM HID]
						OPTFLAGS: [+O1]
						DBGFLAGS: [+g3]
						LIBS: [-LM]
						LDFLAGS: []
						OTHFLAGS: [STX]
				]
			]

	;-------------------------------------------------------------------------
	0.3.01		windows-x86		windows
			[
				GCC [
					PREDEFINES: [LEN LL? UNI]
					CFLAGS: []
					OPTFLAGS: []
					DBGFLAGS: [+g3]
					LIBS: [W32 CDLG]
					LDFLAGS: [S4M CON]
					OTHFLAGS: [EXE DIR]
					FFI: [
						INCLUDES: [%src %include]
						PREDEFINES: [
							EH_FRAME_FLAGS: "aw"
							HAVE_ALLOCA: 1
							HAVE_AS_ASCII_PSEUDO_OP: 1
							HAVE_AS_CFI_PSEUDO_OP: 1
							HAVE_AS_STRING_PSEUDO_OP: 1
							HAVE_AS_X86_PCREL: 1
							HAVE_INTTYPES_H: 1
							HAVE_LONG_DOUBLE: 1
							HAVE_MEMCPY: 1
							HAVE_MEMORY_H: 1
							HAVE_STDINT_H: 1
							HAVE_STDLIB_H: 1
							HAVE_STRINGS_H: 1
							HAVE_STRING_H: 1
							HAVE_SYS_STAT_H: 1
							HAVE_SYS_TYPES_H: 1
							HAVE_UNISTD_H: 1
							PACKAGE: "libffi"
							PACKAGE_NAME: "libffi"
							PACKAGE_VERSION: "3.2.1"
							SIZEOF_DOUBLE: 8
							SIZEOF_LONG_DOUBLE: 12
							SIZEOF_SIZE_T: 4
							STDC_HEADERS: 1
							SYMBOL_UNDERSCORE: 1
							VERSION: "3.2.1"
							TARGET: "X86_WIN32"
						]
						CFLAGS: [
							M32
						]
						OPTFLAGS: [+O3 OFP +SA +FM +EX]
						LIBS: []
						LDFLAGS: []
						TARGET-DIRECTORY: "x86"
						SOURCE: [
							%src/x86/ffi.c
							%src/x86/win32.S
						]
					]
				]
				MSVC [
					PREDEFINES: [LEN LL? UNI FFI-BUILDING]
					CFLAGS: []
					OPTFLAGS: [*O2]
					LIBS: [W32 CDLG]
					LDFLAGS: [S4M_MSVC CON_MSVC]
					OTHFLAGS: [EXE DIR]
					FFI: [
						INCLUDES: [%src %include]
						PREDEFINES: [
							HAVE_CALLOCA: 1
							HAVE_MEMCPY: 1
							HAVE_MEMORY_H: 1
							HAVE_STDINT_H: 1
							HAVE_STDLIB_H: 1
							HAVE_STRING_H: 1
							HAVE_SYS_STAT_H: 1
							HAVE_SYS_TYPES_H: 1
							PACKAGE: "libffi"
							PACKAGE_NAME: "libffi"
							STACK_DIRECTION: -1
							STDC_HEADERS: 1
							FFI_BULIDING: 1
							TARGET: "X86_WIN32"
						]
						CFLAGS: [
						]
						OPTFLAGS: []
						LIBS: []
						LDFLAGS: []
						TARGET-DIRECTORY: "x86"
						SOURCE: [
							%src/x86/ffi.c
						]
					]
				]
			]

	0.3.02		windows-alpha		windows
			[
				GCC [
					PREDEFINES: [LEN LLC]
					CFLAGS: []
					OPTFLAGS: [+O2]
					DBGFLAGS: [+g3]
					LIBS: [LDL ST1 -LM]
					LDFLAGS: []
					OTHFLAGS: []
				]
			]
	0.3.40		windows-x64		windows
			[
				GCC [
					PREDEFINES: [LLP64 LEN LL? UNI]
					CFLAGS: []
					OPTFLAGS: [+O2]
					DBGFLAGS: [+g3]
					LIBS: [W32 CON -LM CDLG]
					LDFLAGS: [S4M]
					OTHFLAGS: [EXE DIR]
					FFI: [
						INCLUDES: [%src %include]
						PREDEFINES: [
							EH_FRAME_FLAGS: "aw"
							HAVE_ALLOCA: 1
							HAVE_AS_CFI_PSEUDO_OP: 1
							HAVE_INTTYPES_H: 1
							HAVE_LONG_DOUBLE: 1
							HAVE_MEMCPY: 1
							HAVE_MEMORY_H: 1
							HAVE_STDINT_H: 1
							HAVE_STDLIB_H: 1
							HAVE_STRINGS_H: 1
							HAVE_STRING_H: 1
							HAVE_SYS_STAT_H: 1
							HAVE_SYS_TYPES_H: 1
							HAVE_UNISTD_H: 1
							PACKAGE: "libffi"
							PACKAGE_NAME: "libffi"
							PACKAGE_VERSION: "3.1.1"
							SIZEOF_DOUBLE: 8
							SIZEOF_LONG_DOUBLE: 16
							SIZEOF_SIZE_T: 8
							STDC_HEADERS: 1
							VERSION: "3.1.1"
							TARGET: "X86_WIN64"
						]
						CFLAGS: [
						]
						OPTFLAGS: [+O3 OFP +SA +FM +EX]
						LIBS: []
						LDFLAGS: []
						TARGET-DIRECTORY: "x86"
						SOURCE: [
							%src/x86/ffi.c
							%src/x86/win64.S
						]
					]
				]
				MSVC [
					PREDEFINES: [LLP64 LEN LL? UNI FFI-BUILDING]
					CFLAGS: []
					OPTFLAGS: [*O2]
					LIBS: [W32 CDLG]
					LDFLAGS: [S4M_MSVC CON_MSVC]
					OTHFLAGS: [EXE DIR]
					FFI: [
						INCLUDES: [%src %include]
						PREDEFINES: [
							HAVE_CALLOCA: 1
							HAVE_MEMCPY: 1
							HAVE_MEMORY_H: 1
							HAVE_STDINT_H: 1
							HAVE_STDLIB_H: 1
							HAVE_STRING_H: 1
							HAVE_SYS_STAT_H: 1
							HAVE_SYS_TYPES_H: 1
							PACKAGE: "libffi"
							PACKAGE_NAME: "libffi"
							PACKAGE_VERSION: "3.1.1"
							SIZEOF_DOUBLE: 8
							SIZEOF_LONG_DOUBLE: 16
							SIZEOF_SIZE_T: 8
							STACK_DIRECTION: -1
							STDC_HEADERS: 1
							FFI_BULIDING: 1
							VERSION: "3.1.1"
							TARGET: "X86_WIN64"
						]
						CFLAGS: [
						]
						OPTFLAGS: []
						LIBS: []
						LDFLAGS: []
						TARGET-DIRECTORY: "x86"
						SOURCE: [
							%src/x86/ffi.c
						]
					]
				]
			]
	;-------------------------------------------------------------------------
	0.4.02		linux-x86		linux
			[
				GCC [
					PREDEFINES: [LEN LLC]
					CFLAGS: []
					OPTFLAGS: [+O2]
					DBGFLAGS: [+g3]
					LIBS: [LDL ST1 -LM LC23]
					LDFLAGS: []
					OTHFLAGS: []
					FFI: [
						PREDEFINES: []
						CFLAGS: []
						OPTFLAGS: [+O2]
						LIBS: [LDL ST1 -LM LC23]
						LDFLAGS: []
					]
				]
			]

	0.4.03		linux-x86		linux
			[
				GCC [
					PREDEFINES: [LEN LLC]
					CFLAGS: []
					OPTFLAGS: [+O2]
					DBGFLAGS: [+g3]
					LIBS: [LDL ST1 -LM LC25]
					LDFLAGS: []
					OTHFLAGS: []
					FFI: [
						PREDEFINES: [
							EH_FRAME_FLAGS: "aw"
							HAVE_ALLOCA_H: 1
							HAVE_AS_ASCII_PSEUDO_OP: 1
							HAVE_AS_STRING_PSEUDO_OP: 1
							HAVE_AS_X86_PCREL: 1
							HAVE_DLFCN_H: 1
							HAVE_HIDDEN_VISIBILITY_ATTRIBUTE: 0
							HAVE_INTTYPES_H: 1
							HAVE_LONG_DOUBLE: 1
							HAVE_MEMCPY: 1
							HAVE_MEMORY_H: 1
							HAVE_MMAP: 1
							HAVE_STDINT_H: 1
							HAVE_STDLIB_H: 1
							HAVE_STRINGS_H: 1
							HAVE_STRING_H: 1
							HAVE_SYS_MMAN_H: 1
							HAVE_SYS_STAT_H: 1
							HAVE_SYS_TYPES_H: 1
							HAVE_UNISTD_H: 1
							STDC_HEADERS: 1
							TARGET: "X86"
						]
						CFLAGS: []
						OPTFLAGS: [+O2]
						LIBS: [LDL ST1 -LM LC23]
						LDFLAGS: []
					]
				]
			]

	0.4.04		linux-x86		linux
			[
				GCC [
					PREDEFINES: [LEN LLC]
					CFLAGS: [M32 HID]
					OPTFLAGS: [+O2]
					DBGFLAGS: [+g3]
					LIBS: [LDL ST1 -LM LC211]
					LDFLAGS: []
					OTHFLAGS: []
					FFI: [
						PREDEFINES: []
						CFLAGS: []
						OPTFLAGS: [+O2]
						LIBS: [LDL ST1 -LM LC23]
						LDFLAGS: []
					]
					FFI: [
						INCLUDES: [%src %include]
						PREDEFINES: [
							EH_FRAME_FLAGS: "a"
							HAVE_ALLOCA: 1
							HAVE_ALLOCA_H: 1
							HAVE_AS_ASCII_PSEUDO_OP: 1
							HAVE_AS_CFI_PSEUDO_OP: 1
							HAVE_AS_STRING_PSEUDO_OP: 1
							HAVE_AS_X86_PCREL: 1
							HAVE_DLFCN_H: 1
							HAVE_HIDDEN_VISIBILITY_ATTRIBUTE: 1
							HAVE_INTTYPES_H: 1
							HAVE_LONG_DOUBLE: 1
							HAVE_MEMCPY: 1
							HAVE_MEMORY_H: 1
							HAVE_MKOSTEMP: 1
							HAVE_MMAP: 1
							HAVE_MMAP_ANON: 1
							HAVE_MMAP_DEV_ZERO: 1
							HAVE_MMAP_FILE: 1
							HAVE_RO_EH_FRAME: 1
							HAVE_STDINT_H: 1
							HAVE_STDLIB_H: 1
							HAVE_STRINGS_H: 1
							HAVE_STRING_H: 1
							HAVE_SYS_MMAN_H: 1
							HAVE_SYS_STAT_H: 1
							HAVE_SYS_TYPES_H: 1
							HAVE_UNISTD_H: 1
							PACKAGE: "libffi"
							PACKAGE_VERSION: "3.2.1"
							SIZEOF_DOUBLE: 8
							SIZEOF_LONG_DOUBLE: 12
							SIZEOF_SIZE_T: 4
							STDC_HEADERS: 1
							VERSION: "3.2.1"
							TARGET: "X86"
						]
						CFLAGS: [
							M32
						]
						OPTFLAGS: [+O3 OFP +SA +FM +EX]
						LIBS: []
						LDFLAGS: []
						TARGET-DIRECTORY: "x86"
						SOURCE: [
							%src/x86/ffi.c
							%src/x86/sysv.S
							%src/x86/win32.S
						]
					]
				]
			]

	0.4.10		linux-ppc		linux
			[
				GCC [
					PREDEFINES: [BEN LLC]
					CFLAGS: []
					OPTFLAGS: [+O1]
					DBGFLAGS: [+g3]
					LIBS: [LDL ST1 -LM]
					LDFLAGS: []
					OTHFLAGS: []
				]
			]

	0.4.11		linux-ppc64		linux
			[LP64 BEN LLC +O1 HID LDL ST1 -LM]

	0.4.20		linux-arm		linux
			[
				GCC [
					PREDEFINES: [LEN LLC]
					CFLAGS: [HID]
					OPTFLAGS: [+O2]
					DBGFLAGS: [+g3]
					LIBS: [LDL ST1 -LM]
					LDFLAGS: []
					OTHFLAGS: []
				]
			]

	0.4.21		linux-arm		linux
			[
				GCC [
					PREDEFINES: [LEN LLC]
					CFLAGS: [HID PIE]
					OPTFLAGS: [+O2]
					DBGFLAGS: [+g3]
					LIBS: [LDL ST1 -LM LCB]
					LDFLAGS: []
					OTHFLAGS: []
				]
			]

	0.4.30		linux-mips		linux
			[
				GCC [
					PREDEFINES: [LEN LLC]
					CFLAGS: [HID]
					OPTFLAGS: [+O2]
					DBGFLAGS: [+g3]
					LIBS: [LDL ST1 -LM]
					LDFLAGS: []
					OTHFLAGS: []
				]
			]

	0.4.31		linux-mips32be	linux
			[BEN LLC +O2 HID LDL ST1 -LM]

	0.4.40		linux-x64		linux
			[
				GCC [
					PREDEFINES: [LP64 LEN LLC]
					CFLAGS: [HID]
					OPTFLAGS: [+O2]
					DBGFLAGS: [+g3]
					LIBS: [LDL ST1 -LM]
					LDFLAGS: []
					OTHFLAGS: []
					FFI: [
						INCLUDES: [%src %include]
						PREDEFINES: [
							HAVE_CONFIG_H: 1
							EH_FRAME_FLAGS: "aw"
							HAVE_ALLOCA_H: 1
							HAVE_AS_ASCII_PSEUDO_OP: 1
							HAVE_AS_STRING_PSEUDO_OP: 1
							HAVE_AS_X86_PCREL: 1
							HAVE_DLFCN_H: 1
							HAVE_INTTYPES_H: 1
							HAVE_LONG_DOUBLE: 1
							HAVE_MEMCPY: 1
							HAVE_MEMORY_H: 1
							HAVE_MMAP: 1
							HAVE_STDINT_H: 1
							HAVE_STDLIB_H: 1
							HAVE_STRINGS_H: 1
							HAVE_STRING_H: 1
							HAVE_SYS_MMAN_H: 1
							HAVE_SYS_STAT_H: 1
							HAVE_SYS_TYPES_H: 1
							HAVE_UNISTD_H: 1
							STDC_HEADERS: 1
							TARGET: "X86_64"
						]

						CFLAGS: [
							CORE2
						]
						OPTFLAGS: [+O3 OFP +SA +FM +EX]
						LIBS: []
						LDFLAGS: []
						TARGET-DIRECTORY: "x86"
						SOURCE: [
							%src/x86/ffi64.c
							%src/x86/unix64.S
							%src/x86/ffi.c
							%src/x86/sysv.S
						]
					]
				]
			]

	;-------------------------------------------------------------------------
	0.5.75		haiku			posix
			[
				GCC [
					PREDEFINES: [LEN LLC]
					CFLAGS: []
					OPTFLAGS: [+O2]
					DBGFLAGS: [+g3]
					LIBS: [ST1 NWK]
					LDFLAGS: []
					OTHFLAGS: []
				]
			]

	;-------------------------------------------------------------------------
	0.7.02		freebsd-x86		posix
			[
				GCC [
					PREDEFINES: [LEN LLC]
					CFLAGS: []
					OPTFLAGS: [+O1]
					DBGFLAGS: [+g3]
					LIBS: [C++ ST1 -LM]
					LDFLAGS: []
					OTHFLAGS: []
				]
			]

	0.7.40		freebsd-x64		posix
			[
				GCC [
					PREDEFINES: [LP64 LEN LLC]
					CFLAGS: []
					OPTFLAGS: [+O1]
					DBGFLAGS: [+g3]
					LIBS: [ST1 -LM]
					LDFLAGS: []
					OTHFLAGS: []
				]
			]

	;-------------------------------------------------------------------------
	0.9.04		openbsd			posix
			[
				GCC [
					PREDEFINES: [LEN LLC]
					CFLAGS: []
					OPTFLAGS: [+O1]
					DBGFLAGS: [+g3]
					LIBS: [C++ ST1 -LM]
					LDFLAGS: []
					OTHFLAGS: []
				]
			]

	;-------------------------------------------------------------------------
	0.13.01		android-arm		android
			[
				GCC [
					PREDEFINES: [LEN LLC]
					CFLAGS: [HID F64]
					OPTFLAGS: []
					DBGFLAGS: [+g3]
					LIBS: [LDL LLOG -LM CST]
					LDFLAGS: []
					OTHFLAGS: []
				]
			]

	;-------------------------------------------------------------------------
	0.14.01		syllable-dtp	posix
			[
				GCC [
					PREDEFINES: [LEN LLC]
					CFLAGS: [HID]
					OPTFLAGS: [+O2]
					DBGFLAGS: [+g3]
					LIBS: [LDL ST1 -LM LC25]
					LDFLAGS: []
					OTHFLAGS: []
				]
			]

	0.14.02		syllable-svr	linux
			[
				GCC [
					PREDEFINES: [LEN LLC]
					CFLAGS: [M32 HID]
					OPTFLAGS: [+O2]
					DBGFLAGS: [+g3]
					LIBS: [LDL ST1 -LM LC211]
					LDFLAGS: []
					OTHFLAGS: []
				]
			]
]

compiler-flags: context [
	M32: "-m32"						; use 32-bit memory model
	ARC: "-arch i386"				; x86 32 bit architecture (OSX)
	CORE2: "-march=core2"			;

	LP64: "__LP64__"				; 64-bit, and 'void *' is sizeof(long)
	LLP64: "__LLP64__"				; 64-bit, and 'void *' is sizeof(long long)

	BEN: "ENDIAN_BIG"				; big endian byte order
	LEN: "ENDIAN_LITTLE"			; little endian byte order
	FFI-BUILDING: "FFI_BUILDING"	; disable __decl(), because it's being staticaly linked

	LLC: "HAS_LL_CONSTS"			; supports e.g. 0xffffffffffffffffLL
	LL?: ""							; might have LL consts, reb-config.h checks

	+OS: "-Os"						; size optimize
	+O1: "-O1"						; optimize for minimal size
	+O2: "-O2"						; optimize for maximum speed
	+O3: "-O3"						; optimize for maximum speed
	+g3: "-g3"						; maximum debugging level
	*O2: "/O2"						; optimize for maximum speed

	UNI: "UNICODE"					; win32 wants it
	CST: "CUSTOM_STARTUP"			; include custom startup script at boot
	HID: "-fvisibility=hidden"		; all syms are hidden
	F64: "_FILE_OFFSET_BITS=64"		; allow larger files
	NPS: "-Wno-pointer-sign"		; OSX fix
	PIC: "-fPIC"					; position independent (used for libs)
	PIE: "-fPIE"					; position independent (executables)
	NCM: "-fno-common"				; lib cannot have common vars
	OFP: "-fomit-frame-pointer"		;
	+SA: "-fstrict-aliasing"
	+FM: "-ffast-math"
	+EX: "-fexceptions"
]

linker-flags: context [
	M32: "-m32"						; use 32-bit memory model (Linux x64)
	ARC: "-arch i386"				; x86 32 bit architecture (OSX)

	NSO: ""							; no shared libs
	C++: "stdc++"					; link with stdc++
	LDL: "dl"						; link with dynamic lib lib
	LLOG: "log"					; on Android, link with liblog.so

	W32: "wsock32"
	CDLG: "comdlg32"
	CON: "-mconsole"				; build as Windows Console binary
	CON_MSVC: "/SUBSYSTEM:CONSOLE"
	S4M: "-Wl,--stack=4194300"
	S4M_MSVC: "/STACK:4194300"
	-LM: "m"						; Math library (Haiku has it in libroot)
	NWK: "network"				; Needed by HaikuOS

	LC23: ""						; libc 2.3
	LC25: ""						; libc 2.5
	LC211: ""						; libc 2.11
	LCB: ""							; bionic (Android)
]

other-flags: context [
	+SC: ""							; has smart console
	-SP: ""							; non standard paths
	COP: ""							; use COPY as cp program
	DIR: ""							; use DIR as ls program
	ST1: "-s"						; strip flags...
	STX: "-x"
	ST2: "-S -x -X"
	CMT: "-R.comment"
	EXE: ""							; use %.exe as binary file suffix
]

; A little bit of sanity-checking on the systems table
comment [
use [rec unknown-flags] [
	; !!! See notes about NO-RETURN in the loop wrapper definition.
	for-each-record-NO-RETURN rec systems [
		assert [tuple? rec/id]
		assert [(to-string rec/os-name) == (lowercase to-string rec/os-name)]
		assert [(to-string rec/os-base) == (lowercase to-string rec/os-base)]
		assert [not find (to-string rec/os-base) charset [#"-" #"_"]]
		assert [block? rec/build-flags]
		for-each flag rec/build-flags [assert [word? flag]]

		; Exclude should mutate (CC#2222), but this works either way
		unknown-flags: exclude (unknown_flags: copy rec/build-flags) compose [
			(words-of compiler-flags)
			(words-of linker-flags)
			(words-of other-flags)
		]
		assert [empty? unknown-flags]
	]
]
]

config-system: func [
	{Return build configuration information}
	/version {Provide a specific version ID}
	id [tuple!] {Tuple matching the OS_ID}
	/guess {Should the function guess the version if it is NONE?}
	hint {Additional value to help with guessing (e.g. commandline args)}
	/local result
][
	; Don't override a literal version tuple with a guess
	if all [guess version] [
		fail "config-system called with both /version and /guess"
	]

	id: any [
		; first choice is a literal tuple that was passed in
		id

		; If version was none and asked to /guess, use opts if given
		if all [guess hint] [
			if block? hint [hint: first hint]
			if hint = ">" [hint: "0.3.1"] ; !!! "bogus cw editor" (?)
			probe hint
			hint: load hint
			unless tuple? hint [
				fail [
					"Expected platform id (tuple like 0.3.1), not:" hint
				]
			]
			hint
		]

		; Fallback: try same version as this r3-make was built with
		to tuple! reduce [0 system/version/4 system/version/5]
	]

	unless result: find-record-unique systems 'id id [
		fail [
			{No table entry for} version {found in systems.r}
		]
	]

	result
]
