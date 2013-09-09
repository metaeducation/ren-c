REBOL [
	Title: "REBOL Core extension generator"
	Author: "Richard Smolak"
	Copyright: {2010 2011 2012 2013 Saphirion AG, Zug, Switzerland}
	License: {
		Licensed under the Apache License, Version 2.0.
		See: http://www.apache.org/licenses/LICENSE-2.0
	}
]

do %make-host-ext.r

emit-file %host-ext-core [
	%../boot/core.r
]
