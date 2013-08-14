REBOL []

do %make-host-init.r

; Files to include in the host program:
files: [
	%mezz/prot-tls.r
	%mezz/prot-http.r
	%mezz/rma-patches.r
]

code: load-files files

save %boot/host-init.r code

write-c-file %include/host-init.h code
