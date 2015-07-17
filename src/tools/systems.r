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
			build-flags]
	;-------------------------------------------------------------------------
	0.1.03		amiga			posix
			[BEN LLC HID NPS +SC CMT COP -SP -LM]
	;-------------------------------------------------------------------------
	0.2.04		osx-ppc			osx
			[BEN LLC +OS NCM -LM NSO]

	0.2.05		osx-x86			osx
			[ARC LEN LLC +O1 NPS PIC NCM HID STX -LM]

	0.2.40		osx-x64			osx
			[LP64 LEN LLC +O1 NPS PIC NCM HID STX -LM]
	;-------------------------------------------------------------------------
	0.3.01		windows-x86		windows
			[LEN LL? +O2 UNI W32 CON S4M EXE DIR -LM]

	0.3.02		windows-x64		windows
			[LLP64 LEN LL? +O2 UNI W32 WIN S4M EXE DIR -LM]
	;-------------------------------------------------------------------------
	0.4.02		linux-x86	    linux
			[LEN LLC +O2 LDL ST1 -LM LC23]

	0.4.03		linux-x86	    linux
			[LEN LLC +O2 HID LDL ST1 -LM LC25]

	0.4.04		linux-x86	    linux
			[M32 LEN LLC +O2 HID LDL ST1 -LM LC211]

	0.4.10		linux-ppc	    linux
			[BEN LLC +O1 HID LDL ST1 -LM]

	0.4.20		linux-arm		linux
			[LEN LLC +O2 HID LDL ST1 -LM LCB]

	0.4.21		linux-arm	    linux
			[LEN LLC +O2 HID LDL ST1 -LM PIE]

	0.4.30		linux-mips		linux
			[LEN LLC +O2 HID LDL ST1 -LM LCM]

	0.4.40		linux-x64	    linux
			[LP64 LEN LLC +O2 HID LDL ST1 -LM]
	;-------------------------------------------------------------------------
	0.5.75		haiku			posix
			[LEN LLC +O2 ST1 NWK]
	;-------------------------------------------------------------------------
	0.7.02		freebsd-x86		posix
			[LEN LLC +O1 C++ ST1 -LM]

	0.7.40		freebsd-x64		posix
			[LP64 LEN LLC +O1 ST1 -LM]
	;-------------------------------------------------------------------------
	0.9.04		openbsd			posix
			[LEN LLC +O1 C++ ST1 -LM]
	;-------------------------------------------------------------------------
	0.13.01	  android-arm   android
			[LEN LLC HID F64 LDL LLOG -LM CST]
	;-------------------------------------------------------------------------
]

compiler-flags: context [
	M32: "-m32"						; use 32-bit memory model
	ARC: "-arch i386"				; x86 32 bit architecture (OSX)

	LP64: "-D__LP64__"				; 64-bit, and 'void *' is sizeof(long)
	LLP64: "-D__LLP64__"			; 64-bit, and 'void *' is sizeof(long long)

	BEN: "-DENDIAN_BIG"				; big endian byte order
	LEN: "-DENDIAN_LITTLE"			; little endian byte order

	LLC: "-DHAS_LL_CONSTS"			; supports e.g. 0xffffffffffffffffLL
	LL?: ""							; might have LL consts, reb-config.h checks

	+OS: "-Os"						; size optimize
	+O1: "-O1"						; optimize for minimal size
	+O2: "-O2"						; optimize for maximum speed

	UNI: "-DUNICODE"				; win32 wants it
	CST: "-DCUSTOM_STARTUP"			; include custom startup script at boot
	HID: "-fvisibility=hidden"		; all syms are hidden
	F64: "-D_FILE_OFFSET_BITS=64"	; allow larger files
	NPS: "-Wno-pointer-sign"		; OSX fix
	NSP: "-fno-stack-protector"		; avoid insert of functions names
	PIC: "-fPIC"					; position independent (used for libs)
	PIE: "-fPIE"					; position independent (executables)
	DYN: "-dynamic"					; optimize for dll??
	NCM: "-fno-common"				; lib cannot have common vars
	PAK: "-fpack-struct"			; pack structures
]

linker-flags: context [
	M32: "-m32"						; use 32-bit memory model (Linux x64)
	ARC: "-arch i386"				; x86 32 bit architecture (OSX)

	NSO: ""							; no shared libs
	MAP: "-Wl,-M"					; output a map
	STA: "--strip-all"
	C++: "-lstdc++"					; link with stdc++
	LDL: "-ldl"						; link with dynamic lib lib
	LLOG: "-llog"					; on Android, link with liblog.so

	W32: "-lwsock32 -lcomdlg32"
	WIN: "-mwindows"				; build as Windows GUI binary
	CON: "-mconsole"				; build as Windows Console binary
	S4M: "-Wl,--stack=4194300"
	-LM: "-lm"						; Math library (Haiku has it in libroot)
	NWK: "-lnetwork"				; Needed by HaikuOS

	LC23: ""						; libc 2.3
	LC25: ""						; libc 2.5
	LC211: ""						; libc 2.11
	LCB: ""							; bionic (Android)
	LCM: ""							; MIPS has glibc without C++
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
use [rec unknown-flags] [
	; !!! See notes about NO-RETURN in the loop wrapper definition.
	foreach-record-NO-RETURN rec systems [
		assert [tuple? rec/id]
		assert [(to-string rec/os-name) = (lowercase to-string rec/os-name)]
		assert [(to-string rec/os-base) = (lowercase to-string rec/os-base)]
		assert [not find (to-string rec/os-base) charset [#"-" #"_"]]
		assert [block? rec/build-flags]
		foreach flag rec/build-flags [assert [word? flag]]

		; Exclude should mutate (CC#2222), but this works either way
		unknown-flags: exclude (unknown_flags: copy rec/build-flags) compose [
			(words-of compiler-flags)
			(words-of linker-flags)
			(words-of other-flags)
		]
		assert [empty? unknown-flags]
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
		do make error! "config-system called with both /version and /guess"
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
				do make error! rejoin [
					"Expected platform id (tuple like 0.3.1), not:" hint
				]
			]
			hint
		]

		; Fallback: try same version as this r3-make was built with
		to tuple! reduce [0 system/version/4 system/version/5]
	]

	unless result: find-record-unique systems 'id id [
		do make error! rejoin [
			{No table entry for} space version space {found in systems.r}
		]
	]

	result
]
