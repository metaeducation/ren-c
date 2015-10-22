; file: https://raw.githubusercontent.com/codebybrett/reb/master/mold-contents.reb
; date: 13-Oct-2015/14:42:42+11:00

REBOL [
	Title: "Mold-Contents"
	Version: 1.0.0
	Rights: {
		Copyright 2015 Brett Handley
	}
	License: {
		Licensed under the Apache License, Version 2.0
		See: http://www.apache.org/licenses/LICENSE-2.0
	}
	Author: "Brett Handley"
	Purpose: {Mold block without the brackets.}
]


mold-contents: func [
	{Mold block without the outer brackets (a little different to MOLD/ONLY).}
	block [block! paren!]
	/local string bol
][

	string: mold block

	either parse/all string [
		skip copy bol [newline some #" "] to end
	][
		replace/all string bol newline
	][
	]
	remove string
	take/last string

	string
]

