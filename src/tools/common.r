REBOL [
	System: "Ren/C Core Extraction of the Rebol System"
	Title: "Common Routines for Tools"
	Rights: {
		Rebol is Copyright 1997-2015 REBOL Technologies
		REBOL is a trademark of REBOL Technologies

		Ren/C is Copyright 2015 MetaEducation
	}
	License: {
		Licensed under the Apache License, Version 2.0
		See: http://www.apache.org/licenses/LICENSE-2.0
	}
	Author: "@HostileFork"
	Version: 2.100.0
	Needs: 2.100.100
	Purpose: {
		These are some common routines used by the utilities
		that build the system, which are found in %src/tools/
	}
]

;-- !!! switch to use spaces when code is transitioned
code-tab: (comment [rejoin [space space space space]] tab)

binary-to-c: func [comp-data /local out data comma-count] [
	; To be "strict" C standard compatible, we do not use a character
	; string literal for data encoded as C due to limits:
	;
	;	http://stackoverflow.com/questions/11488616/
	;
	; We use an array formatted as {0xYY, ...} with 8 bytes per line

	out: make string! 6 * (length? comp-data)
	while [not tail? comp-data] [
		append out code-tab 

		;-- grab in groups of 8
		hexed: enbase/base (copy/part comp-data 8) 16
		comp-data: skip comp-data 8
		foreach [digit1 digit2] hexed [
			append out rejoin [{0x} digit1 digit2 {,} space]
		]

		take/last out ;-- drop the last space
		if tail? comp-data [
			take/last out ;-- lose that last comma
		]
		append out newline ;-- newline after each group, and at end
	]

	;-- Sanity check (should be one more byte in source than commas out)
	parse out [(comma-count: 0) some [thru "," (++ comma-count)] to end]
	assert [(comma-count + 1) = (length? head comp-data)]

	out
]
