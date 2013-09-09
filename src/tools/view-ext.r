REBOL [
	Title: "View extensions code generator"
	Author: "Richard Smolak"
	Copyright: {2010 2011 2012 2013 Saphirion AG, Zug, Switzerland}
	License: {
		Licensed under the Apache License, Version 2.0.
		See: http://www.apache.org/licenses/LICENSE-2.0
	}
]

do %make-host-ext.r

emit-file %host-ext-graphics [
	%../boot/graphics.r
	%../mezz/view-funcs.r
]

emit-file %host-ext-draw [
	%../boot/draw.r
]

emit-file %host-ext-shape [
	%../boot/shape.r
]

emit-file %host-ext-text [
	%../boot/text.r
]
