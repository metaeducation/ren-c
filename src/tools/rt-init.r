REBOL []

do %make-host-init.r

include-vid: off

; Files to include in the host program:
files: [
	%mezz/prot-http.r
;	%mezz/view-colors.r
]

vid-files: [
	%mezz/dial-draw.r
	%mezz/dial-text.r
	%mezz/dial-effect.r
	%mezz/view-funcs.r
	%mezz/vid-face.r
	%mezz/vid-events.r
	%mezz/vid-styles.r
	%mezz/mezz-splash.r
]

if include-vid [append files vid-files]


code: load-files files

save %boot/host-init.r code

write-c-file %include/host-init.h code
