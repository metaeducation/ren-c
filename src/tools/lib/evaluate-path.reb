; file: https://raw.githubusercontent.com/codebybrett/reb/master/evaluate-path.reb
; date: 13-Oct-2015/14:42:41+11:00

REBOL [
	Title: "Evaluate-Path"
	Version: 1.0.0
	Rights: {
		Copyright 2015 Brett Handley
	}
	License: {
		Licensed under the Apache License, Version 2.0
		See: http://www.apache.org/licenses/LICENSE-2.0
	}
	Author: "Brett Handley"
	Purpose: {Evaluate a path. Useful during transition from Rebol 2 to Rebol 3.}
]


either system/version > 2.100.0 [; Rebol3

	either error? try [
		; This should work normally.
		use [x] [x: context [v: 1] get to path! 'x]
	] [
		; Workaround bug.
		evaluate-path: func [path] [
			if 1 = length :path [path: first :path]
			get :path
		]
	] [
		evaluate-path: func [path] [
			get :path
		]
	]

] [; Rebol2

	evaluate-path: func [path] [
		do :path
	]

]
