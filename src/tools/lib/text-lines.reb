; file: https://raw.githubusercontent.com/codebybrett/reb/master/text-lines.reb
; date: 13-Oct-2015/14:42:44+11:00

REBOL [
	Title: "Text Lines"
	Version: 1.0.0
	Rights: {
		Copyright 2015 Brett Handley

		Rebol3 load-next by Chris Ross-Gill.
	}
	License: {
		Licensed under the Apache License, Version 2.0
		See: http://www.apache.org/licenses/LICENSE-2.0
	}
	Author: "Brett Handley"
	Purpose: {Transition load/next from Rebol 2 to Rebol 3.}
]

decode-lines: funct [
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

foreach-line: func [
	{Iterate over text lines.}
	'record [word!] {Word set to metadata for each line.}
	text [string!] {Text with lines.}
	body [block!] {Block to evaluate each time.}
	/local eol
] [

	set/any 'result while [not tail? text] [

		eol: any [
			find text newline
			tail text
		]

		set record compose [position (text) length (subtract index-of eol index-of text)]
		text: next eol

		do body
	]

	get/any 'result
]

lines-exceeding: funct [
	{Return the line numbers of lines exceeding line-length}
	line-length [integer!]
	text [string!]
] [

	count-line: [
		(
			line: 1 + any [line 0]
			if line-length < subtract index-of eol index-of bol [
				length-exceeded: append any [length-exceeded copy []] line
			]
		)
	]

	parse/all text [
		any [bol: to newline eol: skip count-line]
		bol: skip to end eol: count-line
	]

	length-exceeded
]

line-of: funct [
	{Returns line number of position within text.}
	text [string!]
	position [string! integer!]
] [

	if integer? position [
		position: at text position
	]

	count-line: [(line: 1 + any [line 0])]

	parse/all copy/part text next position [
		any [to newline skip count-line] skip count-line
	]

	line
]
