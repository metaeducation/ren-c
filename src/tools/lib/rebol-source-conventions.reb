REBOL [
	Title: "Rebol Source Conventions"
	Version: 1.0.0
	Rights: {
		Copyright 2015 Brett Handley
	}
	License: {
		Licensed under the Apache License, Version 2.0
		See: http://www.apache.org/licenses/LICENSE-2.0
	}
	Author: "Brett Handley"
]

script-needs [
	%load-until-blank.reb
	%mold-contents.reb
]

source-conventions: context [

	std-line-length: 79
	; Not counting newline, lines should be no longer than this.

	max-line-length: 127
	; Not counting newline, lines over this length require an extra warning.
]

c-id-to-word: func [
	{Translate C identifier to Rebol word.}
	identifier
	/local id
] [

	id: select [
		{_add_add} ++
	] identifier

	if not id [
		id: copy identifier
		replace/all id #"_" #"-"
		if #"q" = last id [change back tail id #"?"]
		id: to word! id
	]

	id
]

decode-function-meta: funct [
	{Return [meta notes] from intro text.}
	lines
] [

	result: load-until-blank lines
	if none? result [return none]

	attempt [
		if empty? result/2 [result/2: none]
		result
	]
]

mold-spec: funct [
	{Pretty format a spec to stay within line length.}
	spec [block!]
	width [integer!]
] [

	; TODO: This is a bit clunky, is there a better
	; linebreaking system?  Might be better just working
	; directly from the spec.

	indent-space: {    }

	wsp: compose [some (charset { ^-})]

	rebol-value: parsing-at x [
		res: any [attempt [load-next x] []]
		if not empty? res [value: first res second res]
	]

	emit-line: func [] [

		append result map-each x tokens [x/2]
		append result newline
		clear tokens
	]

	emit-token: func [/local token line-length] [

		if 'newline = type [

			if not empty? tokens [
				token: last tokens
				if find [indent wsp] token/1 [
					remove back tail tokens
				]
			]

			emit-line

			exit
		]

		token: compose [(type) (copy/part start finish)]

		switch type [
			indent [token/2: indent-space]
			lbracket [indent-count: 1 + any [indent-count 0]]
			rbracket [
				if all [(indent-count) = 1 (value-count = 3)][
					append/only tokens compose [comment (join indent-space {; No arguments})]
					emit-line
				]
				indent-count: (any [indent-count 0]) - 1
			]
		]

		if 'value = type [
			value-count: 1 + any [value-count 0]
			if string? value [
				if none? string-instance [
					string-instance: 'first
					if 'indent = first last tokens [
						remove back tail tokens
					]
					emit-line
				]
			]
		]

		line-length: 0 foreach x tokens [line-length: line-length + length x/2]
		if all [
			0 < line-length
			width < (line-length + length token/2)
		] [
			emit-line
			append/only tokens compose [indent (indent-space)]
		]

		append/only tokens token

		if all ['value = type string? value 'first = string-instance] [
			emit-line
			string-instance: 'subsequent
		]

	]

	text: mold-contents spec
	result: make string! 2 * length text
	tokens: make block! []

	if parse/all text [
		any [
			start: [
				[
					newline (type: 'newline) opt [wsp (emit-token) (type: 'indent)]
					| #"[" (type: 'lbracket)
					| #"]" (type: 'rbracket)
					| wsp (type: 'wsp)
					| rebol-value (type: 'value)
				] finish: (emit-token)
			]
		]
	] [
		encode-lines result {} { }
		trim/tail result
		append result newline
		result
	]
]

source-header: {REBOL [
	System: "REBOL [R3] Language Interpreter and Run-time Environment"
	Title: "Native function specs"
	Rights: {
		Copyright 2012 REBOL Technologies
		REBOL is a trademark of REBOL Technologies
	}
	License: {
		Licensed under the Apache License, Version 2.0.
		See: http://www.apache.org/licenses/LICENSE-2.0
	}
	Note: {This is a generated file.}
]
}