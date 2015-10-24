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

proto-parser: context [

	emit-proto: none
	proto-prefix: none

	process: func [data] [parse data rule]

	rule: [any segment]

	segment: [
		thru "/******" to newline [
			format2012.func-header
			| thru newline
		]
	]

	format2012.func-header: [
		format2012.pre.proto
		proto-prefix copy proto to newline newline
		opt format2012.post.comment
		(emit-proto proto)
	]

	format2012.pre.proto: [
		some ["^/**" any [#" " | #"^-"] to newline]
		"^/*/" any [#" " | #"^-"]
	]

	format2012.post.comment: [
		"/*" copy comment-text thru "*/"
	]
]