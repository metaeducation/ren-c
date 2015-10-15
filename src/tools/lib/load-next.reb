; file: https://raw.githubusercontent.com/codebybrett/reb/master/load-next.reb
; date: 13-Oct-2015/14:42:42+11:00

REBOL [
	Title: "Load-Next"
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

either system/version > 2.100.0 [; Rebol3

	load-next: funct [
		{Load the next value. Return block with value and new position.}
		string [string!]
	] [
		out: transcode/next to binary! string
		out/2: skip string subtract length string length to string! out/2
		out
	] ; by @rgchris.

] [; Rebol2

	load-next: funct [
		{Load the next value. Return block with value and new position.}
		string [string!]
	] [
		load/next string
	]
]

