; file: https://raw.githubusercontent.com/codebybrett/reb/master/file-tests.reb
; date: 16-Oct-2015/16:02:57+11:00

REBOL [
	Title: "file tests"
	Version: 1.0.0
	Rights: {
		Copyright 2015 Brett Handley
	}
	License: {
		Licensed under the Apache License, Version 2.0
		See: http://www.apache.org/licenses/LICENSE-2.0
	}
	Author: "Brett Handley"
	Purpose: {Older Rebol 3s on linux do not return a slash at the end of a folder.}
]

folders-of: funct [
	{Return the folders from a block of files.}
	files [block!]
][
	remove-each x files: copy files [
		not all [
			any [file? x url? x]
			equal? #"/" last x
		]
	]
	files
]

either empty? folders-of any [
	attempt [read %../] ; Parent should have this subfolder.
	[]
] [

	; Workaround

	is-dir?: funct [
		{Return true if target is a directory folder.}
		target [file! url!]
	][

		either url? target [
			#"/" = last target
		][
			'dir = exists? target
		]
	]

] [

	is-dir?: funct [
		{Return true if target is a directory folder.}
		target [file! url!]
	][

		#"/" = last target
	]
]

