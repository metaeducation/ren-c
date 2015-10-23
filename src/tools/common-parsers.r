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


format2012.post.comment: [
	"/*" ; must be in func header section, not file banner
	any [
		thru "**" [#" " | #"^-"] copy line thru newline
	]
	thru "*/"
]