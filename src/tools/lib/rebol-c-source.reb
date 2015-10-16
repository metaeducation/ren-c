REBOL [
	Title: "Rebol C Source"
	Version: 1.0.0
	Rights: {
		Copyright 2015 Brett Handley
	}
	License: {
		Licensed under the Apache License, Version 2.0
		See: http://www.apache.org/licenses/LICENSE-2.0
	}
	Author: "Brett Handley"
	Purpose: {Process Rebol C source.}
]

;
;
; References to the old format (search 2012) should be removed after conversion.
;

script-needs [
	https://raw.githubusercontent.com/codebybrett/grammars/master/C/c-lexicals.reb
	%parse-kit.reb
	%read-below.reb
	%trees.reb
	%text-lines.reb
	%mold-contents.reb
	%rebol-source-conventions.reb
]

rebol-c-source: context [

	src-folder: none
	; Path to src/

	logfn: func [message] [print mold new-line/all compose/only message false]

	log: none ; Set to a log function for logging.

	proto-exclusions: [
		{REBNATIVE(in_context)}
		{REBNATIVE(native)}
		{REBNATIVE(action)}
	] ; Is there a better way to handle this?

	whitelisted: [
		%core/u-bmp.c 
		%core/u-compress.c 
		%core/u-gif.c 
		%core/u-jpg.c 
		%core/u-md5.c 
		%core/u-png.c 
		%core/u-sha1.c 
		%core/u-zlib.c
	] ; Not analysed for errors...

	std-line-length: source-conventions/std-line-length
	; Not counting newline.

	max-line-length: source-conventions/max-line-length
	; Not counting newline.

	file-warnings: on
	; True or not.

	; --- End config.

	cached: context [
		files:
		functions:
		none
	]

	grammar: context bind [

		rule: [some segment]

		segment: [
			function-section
			| span-comment | line-comment
			| other-section
		]

		function-section: [
			convention-2012
			| convention-2015 
		]

		convention-2012: [
			convention-2012.intro
			function.decl
			function.post-decl
		] ; Should be removed after conversion to convention-2015.

		convention-2012.intro: [
			"/***" 10 100 "*" newline
			thru "*/" any [#" " | #"^-"]
		] ; Should be removed after conversion to convention-2015.

		convention-2012.post-proto: [
			"/*"
			any [thru "**" [#" " | #"^-"] thru newline]
			thru "*/" newline
		] ; Should be removed after conversion to convention-2015.

		convention-2015: [
			opt intro-section
			function.decl
			function.post-decl
		]

		to-function: [any [not-function-section segment]]
		; Used by find-function.

		not-function-section: parsing-unless function-section

		intro-section: [intro-comment any eol]

		intro-comment: [some [line-comment eol]]
		not-intro: parsing-unless [[convention-2012.intro | intro-comment] function.decl]

		other-section: [some [not-intro c-pp-token]]

		function.decl: [function.words function.args [eol | opt wsp eol]]

		function.post-decl: [
			opt convention-2012.post-proto
			is-lbrace
		]

		function.words: [function.id opt wsp any [[function.id | function.star] opt wsp]]
		function.args: [#"(" any [function.id | wsp | not-rparen punctuator] #")"]
		function.id: copy identifier
		function.star: #"*"

		is-punctuator: parsing-when punctuator
		is-lbrace: parsing-when [is-punctuator #"{"]
		not-rparen: parsing-unless [#")"]

	] c.lexical/grammar

	valid?: funct [{Return true if the text can be parsed by the grammar.} text] [

		parse/all/case text grammar/rule
	]

	parser: context [

		find-function: funct [{Finds function in text.} text] [

			parse/all/case text [
				grammar/to-function
				position:
			]

			if not tail? position [
				position
			]
		]

		foreach-func: func [
			{Iterate function sections.}
			'record [word!] {Word set to function metadata for each function.}
			text [string!] {C source text.}
			body [block!] {Block to evaluate each time.}
			/local position intro spec result meta notes
		] [

			position: text

			set/any 'result while [text: find-function position] [

				if same? text position [
					fail [{Failed to parse function beyond position} (index-of position)]
				]

				spec: parse-function-section/next text 'position

				if spec/error [
					fail [(spec/error) {at position} (index-of text) (mold spec/proto)]
				]

				set record spec

				do body
			]

			get/any 'result
		]

		parse-decl: funct [
			{Load function declaration.}
			string [string!]
			/next {Set a variable with next position.}
			var [word!] "Variable updated with new block position"
		] [

			terms: bind [function.words function.args] grammar
			terminals: bind [function.id function.star] grammar

			tree: get-parse/terminal [
				parse/all/case string [grammar/function.decl position:]
			] terms terminals

			if empty? at tree 4 [
				return none ; Not a valid declaration.
			]

			if next [set var position]

			using-tree-content tree

			words: map-each node at tree/4 4 [assert [find [function.id function.star] node/1] node/3/content]

			if empty? at tree 5 [
				prettify-tree tree
				?? tree
				fail [{Could not parse declaration arguments. Near:} (copy/part string find string newline)]
			]

			args: map-each node at tree/5 4 [assert [find [function.id function.star] node/1] node/3/content]

			reduce [words args]
		]

		parse-function-section: funct [
			{Load function section.}
			text
			/next {Set a variable with next position.}
			var [word!] "Variable updated with new block position"
		] [

			set [meta notes] none

			convention-2012: parse-2012-intro/next text 'position

			unless convention-2012 [
				set [meta notes] parse-intro/next text 'position
			]

			decl: parse-decl/next start: any [position text] 'position

			if none? position [
				near: (copy/part text find start newline)
				print "Error near:"
				print near
				fail [{Could not parse function declaration at position index} (index-of start)]
			]

			proto: trim/tail copy/part start position

			if next [set var position]

			error: case [

				none? decl {Could not parse function declaration.}

				if all [
					not convention-2012
					equal? {REBNATIVE} first decl/1
					not find proto-exclusions proto
				] [
					not all [
						attempt ['native = meta/2]
						attempt [equal? to word! meta/1 c-id-to-word last decl/2]
					]
				] {Invalid metadata for function.}
			]

			position: compose [
				intro (index-of text)
				proto (index-of start)
				body (index-of position)
			]

			spec: collect [
				foreach word [proto decl meta notes position error convention-2012] [
					keep word
					keep/only get word
				]
			]

			new-line/all/skip spec true 2

			spec
		]

		parse-2012-intro: funct [
			{Load function introduction comment.}
			string
			/next {Set a variable with next position.}
			var [word!] "Variable updated with new block position"
		] [

			if none? string [return none]

			; convention-2012 only - to be removed after conversion
			parse/all string [grammar/convention-2012.intro position:]

			if next [set var position]
			if position [
				true
			]
		] ; To be removed after conversion to convention-2015.

		parse-intro: funct [
			{Load function introduction comment.}
			string
			/next {Set a variable with next position.}
			var [word!] "Variable updated with new block position"
		] [

			if none? string [return none]

			parse/all string [grammar/intro-section position: (prefix: {//})]

			if next [set var position]
			if position [

				lines: copy/part string position
				lines: attempt [decode-lines lines prefix {}]
				; The indent is subject to manual edits, don't trust it.

				if not lines [return none]

				decode-function-meta lines
			]
		]

		parse-source-functions: funct [
			{Parse function specs from C source.}
			text [string!]
		] [

			collect [

				foreach-func spec text [

					analysis: analyse/function-section text spec
					insert pos: tail spec compose/only [
						line (line-of text spec/position/intro)
						analysis (analysis)
					]
					new-line pos true

					spec/position: spec/position/intro

					keep/only new-line/all/skip spec true 2
				]
			]
		]
	]

	analyse: context [

		file: funct [
			{Analyse a file returning facts.}
			file
			text
			meta
		][

			if whitelisted? file [return none]

			; TODO: Review/refactor.

			new-line/all collect [

				if file-warnings [

					if non-std-lines: lines-exceeding std-line-length text [
						keep/only compose/only [line-exceeds (std-line-length) (file) (non-std-lines)]
					]

					if overlength-lines: lines-exceeding max-line-length text [
						keep/only compose/only [line-exceeds (max-line-length) (file) (overlength-lines)]
					]

					wsp-not-eol: exclude c.lexical/charsets/ws-char charset {^/}
					eol-wsp: malloc: none
					file-text: text
					do bind [
						is-identifier: parsing-when [identifier]
						eol-wsp-check: [wsp-not-eol eol (append any [eol-wsp eol-wsp: copy []] line-of file-text position)]
						malloc-check: [is-identifier "malloc" (append any [malloc malloc: copy []] line-of file-text position)]
						parse/all/case file-text [
							some [
								position:
								malloc-check
								| eol-wsp-check
								| c-pp-token
							]
						]
					] c.lexical/grammar
					if eol-wsp [
						keep/only compose/only [eol-wsp (file) (eol-wsp)]
					]
					if malloc [
						keep/only compose/only [malloc (file) (malloc)]
					]
				]

			] true
		]

		function-section: funct [
			{Analyse a function returning facts.}
			text
			meta
		] [
			none
		]
	]

	generate: context [

		natives.r: funct [
			{Generate natives.r}
			natives [block!] {As returned from scan/natives.}
		] [
			natives: copy natives
			remove-each native natives [
				any [
					native/convention-2012
					'none = native/meta/3
				]
			]

			head collect/into [

				keep source-header

				foreach native natives [
					keep rejoin [
						newline
						{; !!! DO NOT EDIT HERE! This is generated from }
						mold native/file { line } native/line newline
						mold-contents native/meta
					]
				]

				keep {^/;-- Expectation is that evaluation ends in UNSET!, empty parens makes one
()
}

			] make string! 50000
		]
	]

	list: context [

		c-file: funct [{Retrieves a list of .c scripts (relative paths).}] [

			if not src-folder [
				fail {Configuration required.}
			]

			files: read-below src-folder
			remove-each file files [not parse/all file [thru {.c}]]
			sort files

			files
		]

		natives: funct [
			{Get native specs from C source files.}
		] [

			if none? cached/functions [scan]

			remove-each fn result: copy cached/functions [
				not equal? "REBNATIVE" fn/decl/1/1
			]

			result
		]

		warnings: funct [{Retrieves analysis from the files.}][

			if not cached/files [
				either file-warnings [
					fail "Scan required."
				][
					fail "File warnings are off, turn on and rescan."
				]
			]

			collect [
				foreach [file data] cached/files [
					if data/analysis [keep data/analysis]
				]
			]
		]
	]

	scan: funct [
		{Loads function specs from C source files.}
	] [

		log [scan-root (src-folder)]

		file-list: list/c-file

		log [scan-file-count (length file-list)]

		cached/files: make block! 2 * length file-list
		cached/functions: make block! 300

		foreach file file-list [

			log [scan (file)]

			text: read/string src-folder/:file

			function-list: parser/parse-source-functions text

			foreach spec function-list [

				insert spec compose [
					file (file)
				]

				insert/only tail pos: cached/functions spec
				new-line pos true
			]

			file-data: compose/only [
				functions (function-list)
			]

			append cached/files reduce [file file-data]

			analysis: analyse/file file text file-data
			insert pos: tail file-data compose/only [analysis (analysis)]
			new-line pos true
		]

		new-line/all/skip cached/files true 2
	]

	whitelisted?: funct [{Returns true if file should not be analysed.} file] [

		if find whitelisted file [true]
	]
]
