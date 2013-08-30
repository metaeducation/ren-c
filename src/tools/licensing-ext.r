REBOL []

do %make-host-ext.r

emit-file %host-ext-licensing [
	%../../../c-code/extensions/licensing/include/make-ext-input.r3 
]
