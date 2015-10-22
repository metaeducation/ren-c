; file: https://raw.githubusercontent.com/codebybrett/reb/master/load-until-blank.reb
; date: 13-Oct-2015/14:42:42+11:00

REBOL [
	Title: "Load-Until-Blank"
	Version: 1.0.0
	Rights: {
		Copyright 2015 Brett Handley
	}
	License: {
		Licensed under the Apache License, Version 2.0
		See: http://www.apache.org/licenses/LICENSE-2.0
	}
	Author: "Brett Handley"
	Purpose: {Load a rebol block from a string until double newline.}
]

script-needs [
	%load-next.reb
	%parse-kit.reb
]

load-until-blank: funct [
	{Load rebol values from text until double newline.}
	text [string!]
	/next {Return values and next position.}
][

	wsp: compose [some (charset { ^-})]

	rebol-value: parsing-at x [
		res: any [attempt [load-next x] []]
		if not empty? res [second res]
	]

	terminator: [opt wsp newline opt wsp newline]

	not-terminator: parsing-unless terminator
	; Could be replaced with Not in Rebol 3 parse.

	rule: [
		some [not-terminator rebol-value]
		opt wsp opt [1 2 newline] position: to end
	]

	if parse/all text rule [
		values: load copy/part text position
		reduce [values position]
	]
]
