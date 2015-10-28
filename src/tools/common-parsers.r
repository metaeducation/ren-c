REBOL [
	System: "Ren/C Core Extraction of the Rebol System"
	Title: "Common Parsers for Tools"
	Rights: {
		Rebol is Copyright 1997-2015 REBOL Technologies
		REBOL is a trademark of REBOL Technologies

		Ren/C is Copyright 2015 MetaEducation
	}
	License: {
		Licensed under the Apache License, Version 2.0
		See: http://www.apache.org/licenses/LICENSE-2.0
	}
	Author: "@codebybrett"
	Version: 2.100.0
	Needs: 2.100.100
	Purpose: {
		These are some common routines used by the utilities
		that build the system, which are found in %src/tools/
	}
]

decode-lines: function [
	{Decode text previously encoded using a line prefix e.g. comments (modifies).}
	text [string!]
	line-prefix [string!] {Usually "**" or "//".}
	indent [string!] {Usually "  ".}
] [
	if not parse/all text [any [line-prefix thru newline]] [
		fail [{decode-lines expects each line to begin with} (mold line-prefix) { and finish with a newline.}]
	]
	insert text newline
	replace/all text join newline line-prefix newline
	if not empty? indent [
		replace/all text join newline indent newline
	]
	remove text
	remove back tail text
	text
]


encode-lines: func [
	{Encode text using a line prefix (e.g. comments).}
	text [string!]
	line-prefix [string!] {Usually "**" or "//".}
	indent [string!] {Usually "  ".}
	/local bol pos
] [

	; Note: Preserves newline formatting of the block.

	; Encode newlines.
	replace/all text newline rejoin [newline line-prefix indent]

	; Indent head if original text did not start with a newline.
	pos: insert text line-prefix
	if not equal? newline pos/1 [insert pos indent]

	; Clear indent from tail if present.
	if indent = pos: skip tail text 0 - length indent [clear pos]
	append text newline

	text
]

load-next: function [
	{Load the next value. Return block with value and new position.}
	string [string!]
] [
	out: transcode/next to binary! string
	out/2: skip string subtract length string length to string! out/2
	out
] ; by @rgchris.


load-until-blank: function [
	{Load rebol values from text until double newline.}
	text [string!]
	/next {Return values and next position.}
] [

	wsp: compose [some (charset { ^-})]

	rebol-value: parsing-at x [
		res: any [attempt [load-next x] []]
		if not empty? res [second res]
	]

	terminator: [opt wsp newline opt wsp newline]

	rule: [
		some [not terminator rebol-value]
		opt wsp opt [1 2 newline] position: to end
	]

	if parse text rule [
		values: load copy/part text position
		reduce [values position]
	]
]


parsing-at: func [
	{Defines a rule which evaluates a block for the next input position, fails otherwise.}
	'word [word!] {Word set to input position (will be local).}
	block [block!] {Block to evaluate. Return next input position, or none/false.}
	/end {Drop the default tail check (allows evaluation at the tail).}
] [
	use [result position] [
		block: to paren! block
		if not end [
			block: compose/deep/only [all [not tail? (word) (block)]]
		]
		block: compose/deep [result: either position: (block) [[:position]] [[end skip]]]
		use compose [(word)] compose/deep [
			[(to set-word! :word) (to paren! block) result]
		]
	]
]


proto-parser: context [

	emit-proto: none
	proto-prefix: none
	notes.post: none
	lines: none
	data: none
	style: none

	process: func [data] [parse data grammar/rule]

	grammar: context [

		rule: [any segment]

		segment: [
			format2015-func-header
			| format2012-func-header
			| thru newline
		]

		format2015-func-header: [
			doubleslashed-lines
			and is-format2015-intro
			proto-prefix copy proto to newline newline
			(
				style: 'format2015
				emit-proto proto
			)
		]

		doubleslashed-lines: [copy lines some ["//" thru newline]]

		is-format2015-intro: parsing-at position [
			if all [
				lines: attempt [decode-lines lines {//} { }]
				data: load-until-blank lines
				data: attempt [
					if set-word? first data/1 [
						notes.post: data/2
						data/1
					]
				]
			] [
				position ; Success.
			]
		]

		format2012-func-header: [
			"/******" to newline
			some ["^/**" any [#" " | #"^-"] to newline]
			"^/*/" any [#" " | #"^-"]
			proto-prefix copy proto to newline newline
			opt ["/*" newline copy notes.post to "*/" "*/"]
			(
				style: 'format2012
				emit-proto proto
			)
		]
	]
]
