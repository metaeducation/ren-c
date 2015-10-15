; file: https://raw.githubusercontent.com/codebybrett/reb/master/env.reb
; date: 14-Oct-2015/18:38:48+11:00

REBOL [
	Title: "Environment"
	Version: 1.0.0
	Rights: {
		Copyright 2015 Brett Handley
	}
	License: {
		Licensed under the Apache License, Version 2.0
		See: http://www.apache.org/licenses/LICENSE-2.0
	}
	Author: "Brett Handley"
	Purpose: {Provide environment for scripts.}
]

; -----------------------------------------------------------------------------
;
; env
;
;	Defines an environment for scripts.
;
;
; script-needs
;
;	Script-Needs is intended to be metadata for a script.
;	Each script should use script-needs only once near the top of the script.
;
; -----------------------------------------------------------------------------


;
; TODO:
;
;	* Refresh file from url other than reb. Need a manifest?
;

if not value? 'fail [
	do %r2r3-future.r
]

env: context [

	master: https://raw.githubusercontent.com/codebybrett/reb/master/

	base: either find [url!] type-of/word system/script/args [
		system/script/args
	][
		what-dir
	]

	logfn: func [message] [print mold new-line/all compose/only message false]
	log: none ; Set to logfn for logging.

	log [env (compose [base (base) master (master)])]

	scripts: context [

		used: make block! []

		refresh: funct [
			{Attempt to refresh each script in base directory from master.}
		] [

			files: read base
			remove-each file files [not parse/all file [thru %.reb | thru %.r]]

			foreach file files [
				either text: attempt [read location: master/:file][
					log [refresh true (file)]
					write base/:file rejoin [{; file: } location {^/; date: } now {^/^/} text]
				][
					log [refresh false (file)]
				]
			]
			clear used
			exit
		]
	]

	conditions?: func [
		{Return true if the conditions are met. See script-environment.}
		conditions [block!]
	] [
		all bind/copy conditions facts
	]

	facts: context [
		rebol3: (system/version > 2.100.0)
		rebol2: (system/version < 2.100.0)
		view: found? find form system/product {view}
	]

	retrieve: funct [
		{Retrieve a script.}
		pattern [file! url!]
	][

		path: name: none
		set [path name] split-path pattern

		failed: make block! []

		read-script: func [fullpath][

			if not find failed fullpath [
				script: context compose [
					id: (name)
					file: (fullpath)
					text: attempt [
						append failed fullpath
						log [attempt-read (fullpath)]
						read fullpath
					]
					time: now/precise
				]
			]

			script/text
		]

		either any [url? path path = %./] [

			; Search for file if not already used.

			either cached: find scripts/used name [
				log [already-used (name)]
			][

				any [
					read-script name
					read-script base/:name
					if url? pattern [read-script pattern]
					read-script master/:name
				]
			]
		] [

			; Specific file - run each time.

			read-script pattern
		]

		if all [
			not cached
			none? script/text
		] [
			fail [{Could not retrieve} (mold pattern)]
		]

		script
	]

	run: funct [
		{Run a script.}
		search-file [file! url!]
	] [

		script: retrieve search-file

		either script [

			file: clean-path script/file
			log [run (search-file)]
			do script/text
			def: compose [
				file (script/file)
				;; search (search-file)
				;; time (script/time)
			]
			new-line/all/skip def true 2
			insert position: tail scripts/used reduce [script/id def]
			new-line position true
		][

			log [not-run (search-file)]
		]

		file
	]

]


script-needs: funct [
	{Runs each script listed. Accepts mulitple files. A block preceeding a file is a predicate to be evaluated by script-environment?.}
	needs [file! block!]
	/local script
] [
	needs: compose [(:needs)]
	if not parse needs [
		any [
			pos: (pred: true) opt [set pred block! (pred: env/conditions? pred)]
			set script [file! | url!] (if pred [env/run :script])
		]
	] [
		fail [{Could not parse script-needs near:} (copy/part pos 40)]
	]
]

env/base
