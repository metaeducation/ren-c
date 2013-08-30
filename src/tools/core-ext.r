REBOL []

do %make-host-ext.r

emit-file %host-ext-core [
	%../boot/core.r
]
