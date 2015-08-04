REBOL [
	System: "REBOL [R3] Language Interpreter and Run-time Environment"
	Title: "REBOL 3 Boot Sys: Startup"
	Rights: {
		Copyright 2012 REBOL Technologies
		REBOL is a trademark of REBOL Technologies
	}
	License: {
		Licensed under the Apache License, Version 2.0
		See: http://www.apache.org/licenses/LICENSE-2.0
	}
	Context: sys
	Note: {
		Originally Rebol's "Mezzanine" init was one function.  In Ren/C's
		philosophy of being "just an interpreter core", many concerns will
		not be in the basic library.  This includes whether "--do" is the
		character sequence on the command-line for executing scripts (or even
		if there *is* a command-line).  It also shouldn't be concerned with
		code for reading embedded scripts out of various ELF or PE file
		formats for encapping.

		Prior to the Atronix un-forking, Ren/C had some progress by putting
		the "--do" handling into the host.  But merging the Atronix code put
		encapping into the core startup.  So this separation will be an
		ongoing process.  For the moment that is done by splitting into two
		functions: a "core" portion for finishing Init_Core(), and a "host"
		portion for finishing RL_Start().

		!!! "The boot binding of this module is SYS then LIB deep.
		Any non-local words not found in those contexts WILL BE
		UNBOUND and will error out at runtime!"
	}
]

finish-init-core: func [
	"Completes the boot sequence for Ren/C core."
	/local tmp
] bind [ ; context is system/options

	; For now, we consider initializing the port schemes to be "part of the
	; core function".  Longer term, it may be the host's responsibility to
	; pick and configure the specific schemes it wishes to support...or
	; to delegate to the user to load them.
	;
	init-schemes

	; Make the user's global context
	;
	tmp: make object! 320
	append tmp reduce ['system :system]
	system/contexts/user: tmp

	; Remove the reference through which this function we are running is
	; found, so it's invisible to the user and can't run again.
	;
	finish-init-core: 'done

	; Set the "boot-level"
	; !!! Is this something the user needs to be concerned with?
	;
	assert [none? boot-level]
	boot-level: 'full

	; It was a stated goal at one point that it should be possible to protect
	; the entire system object and still run the interpreter.  This was
	; commented out, so the state of that feature is unknown.
	;
	comment [if :lib/secure [protect-system-object]]

	; returning anything but NONE! from init is considered an error, and
	; the value is raised as an alert when Panic()-ing
	;
	none

] system/options


finish-rl-start: func [
	"Loads extras, handles args, security, scripts (should be host-specific)."
	/local file script-path script-args code
] bind [ ; context is: system/options

	;-- Print minimal identification banner if needed:
	if all [
		not quiet
		any [flags/verbose flags/usage flags/help]
	][
		boot-print boot-banner ; basic boot banner only
	]
	if any [boot-embedded do-arg script] [quiet: true]

	;-- Set up option/paths for /path, /boot, /home, and script path (for SECURE):
	path: dirize any [path home]

	;-- !!! this was commented out, and said "HAVE C CODE DO IT PROPERLY !!!!"
	comment [
		if slash <> first boot [boot: clean-path boot]
	]

	home: file: first split-path boot
	if file? script [ ; Get the path (needed for SECURE setup)
		script-path: split-path script
		case [
			slash = first first script-path []		; absolute
			%./ = first script-path [script-path/1: path]	; curr dir
			'else [insert first script-path path]	; relative
		]
	]

	;-- Convert command line arg strings as needed:
	script-args: args ; save for below
	foreach [opt act] [
		args    [if args [parse args ""]]
		do-arg  block!
		debug   block!
		secure  word!
		import  [if import [to-rebol-file import]]
		version tuple!
	][
		set opt attempt either block? act [act][
			[all [get opt to get act get opt]]
		]
	]
	; version, import, secure are all of valid type or none

	if flags/verbose [print self]

	;-- Boot up the rest of the run-time environment:
	;   NOTE: this can still be split up into more boot-levels !!!
	;   For example: mods, plus, host, and full
	if boot-level [
		load-boot-exts
		loud-print "Init mezz plus..."

		do bind-lib boot-mezz
		boot-mezz: 'done

		foreach [spec body] boot-prot [module spec body]
		;do bind-lib boot-prot
		;boot-prot: 'done

		;-- User is requesting usage info:
		if flags/help [lib/usage quiet: true]

		;-- Print fancy banner (created by mezz plus):
		if any [
			flags/verbose
			not any [quiet script do-arg]
		][
			boot-print boot-banner
		]
		if boot-host [
			loud-print "Init host code..."
			do load boot-host
			boot-host: none
		]
	]

	;-- Setup SECURE configuration (a NO-OP for min boot)
	lib/secure (case [
		flags/secure [secure]
		flags/secure-min ['allow]
		flags/secure-max ['quit]
		file? script [compose [file throw (file) [allow read] (first script-path) allow]]
		'else [compose [file throw (file) [allow read] %. allow]] ; default
	])

	;-- Evaluate rebol.r script:
	loud-print ["Checking for rebol.r file in" file]
	if exists? file/rebol.r [do file/rebol.r] ; bug#706

	;boot-print ["Checking for user.r file in" file]
	;if exists? file/user.r [do file/user.r]

	boot-print ""

	; Import module?
	if import [lib/import import]

	unless none? boot-embedded [
		code: load/header/type boot-embedded 'unbound
		;boot-print ["executing embedded script:" mold code]
		system/script: make system/standard/script [
			title: select first code 'title
			header: first code
			parent: none
			path: what-dir
			args: script-args
		]
		either 'module = select first code 'type [
			code: reduce [first+ code code]
			if object? tmp: do-needs/no-user first code [append code tmp]
			import make module! code
		][
			do-needs first+ code
			do intern code
		]
		quit ;ignore user script and "--do" argument
	]

	;-- Evaluate script argument?
	either file? script [
		; !!! Would be nice to use DO for this section. !!!
		; NOTE: We can't use DO here because it calls the code it does with CATCH/quit
		;   and we shouldn't catch QUIT in the top-level script, we should just quit.
		; script-path holds: [dir file] for script
		assert/type [script-path [block!] script-path/1 [file!] script-path/2 [file!]]
		; /path dir is where our script gets started.
		change-dir first script-path
		either exists? second script-path [
			boot-print ["Evaluating:" script]
			code: load/header/type second script-path 'unbound
			; update system/script (Make into a function?)
			system/script: make system/standard/script [
				title: select first code 'title
				header: first code
				parent: none
				path: what-dir
				args: script-args
			]
			either 'module = select first code 'type [
				code: reduce [first+ code code]
				if object? tmp: do-needs/no-user first code [append code tmp]
				import make module! code
			][
				do-needs first+ code
				do intern code
			]
			if flags/halt [lib/halt]
		] [
			cause-error 'access 'no-script script
		]
	][
		boot-print boot-help
	]

	finish-rl-start: 'done

	none ; returning anything besides none is considered failure
] system/options
