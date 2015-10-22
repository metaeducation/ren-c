; file: https://raw.githubusercontent.com/codebybrett/reb/master/apropos.reb
; date: 13-Oct-2015/14:42:41+11:00

REBOL [
	Title: "Apropos"
	Version: 1.0.0
	Rights: {
		Copyright 2015 Brett Handley
	}
	License: {
		Licensed under the Apache License, Version 2.0
		See: http://www.apache.org/licenses/LICENSE-2.0
	}
	Author: "Brett Handley"
	Purpose: {Bind and evaluate expressions relative to contexts.}
]

script-needs [
	%evaluate-path.reb
]

apropos: func [
	{Bind and evaluate block using one or more context references.}
	reference [object! word! block! path!] {Represents context.}
	body [block! paren!]
	/binding {Just bind, do not evaluate the block}
	/only {Evaluate the path only, not each segment of the path.}
] [

	switch/default type-of/word :reference [

		object! [
			bind body reference
		]

		block! [
			foreach context reference [apropos/binding context body]
		]

		path! [
			if not only [
				for i 1 (subtract length reference 1) 1 [
					bind body evaluate-path copy/part reference i
				]
			]
			bind body evaluate-path reference
		]

		word! [bind body do reference]

	] [fail {APROPOS only accepts simple references to contexts.}]

	either binding [body] [do body]
]

