REBOL []

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
