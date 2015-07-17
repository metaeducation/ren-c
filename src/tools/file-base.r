REBOL [
	System: "REBOL [R3] Language Interpreter and Run-time Environment"
	Title: "Source File Database"
	Rights: {
		Copyright 2012 REBOL Technologies
		REBOL is a trademark of REBOL Technologies
	}
	License: {
		Licensed under the Apache License, Version 2.0
		See: http://www.apache.org/licenses/LICENSE-2.0
	}
	Author: "Carl Sassenrath"
	Purpose: {
		Lists of files used for creating makefiles.
	}
]

core: [
	a-constants.c
	a-globals.c
	a-lib.c

; Empty...
;	a-lib2.c

	a-stubs.c
	b-boot.c
	b-init.c

; Non-functional
;	b-main.c

	c-do.c
	c-error.c
	c-frame.c
	c-function.c
	c-port.c
	c-task.c
	c-word.c
	d-crash.c
	d-dump.c
	d-print.c
	f-blocks.c
	f-deci.c
	f-dtoa.c
	f-enbase.c
	f-extension.c
	f-int.c
	f-math.c
	f-modify.c
	f-qsort.c
	f-random.c
	f-round.c
	f-series.c
	f-stubs.c
	l-scan.c
	l-types.c
	m-gc.c
	m-pools.c
	m-series.c
	n-control.c
	n-data.c
	n-io.c
	n-loop.c
	n-math.c
	n-sets.c
	n-strings.c
	n-system.c
	p-clipboard.c
	p-console.c
	p-dir.c
	p-dns.c
	p-event.c
	p-file.c
	p-net.c
	p-serial.c
	p-signal.c

; Marked as unimplemented
;	p-timer.c

	s-cases.c
	s-crc.c
	s-file.c
	s-find.c
	s-make.c
	s-mold.c
	s-ops.c
	s-trim.c
	s-unicode.c
	t-bitset.c
	t-block.c
	t-char.c
	t-datatype.c
	t-date.c
	t-decimal.c
	t-event.c
	t-function.c
	t-gob.c
	t-image.c
	t-integer.c
	t-library.c
	t-logic.c
	t-map.c
	t-money.c
	t-none.c
	t-object.c
	t-pair.c
	t-port.c
	t-routine.c
	t-string.c
	t-struct.c
	t-time.c
	t-tuple.c
	t-typeset.c
	t-utype.c
	t-vector.c
	t-word.c
	u-bmp.c
	u-compress.c
	u-dialect.c
	u-gif.c
	u-jpg.c
	u-md5.c
	u-parse.c
	u-png.c
	u-sha1.c
	u-zlib.c

	; Atronix repository breaks out codecs into a separate directory.
	; More crypto is needed than in original Rebol open source for the HTTPS
	; protocol implementation.

	../codecs/aes/aes.c
	../codecs/bigint/bigint.c
	../codecs/dh/dh.c
	../codecs/png/lodepng.c
	../codecs/rc4/rc4.c
	../codecs/rsa/rsa.c
]

made: [
	make-boot.r			core/b-boot.c
	make-headers.r		include/tmp-funcs.h

; Ren/C is core sources with no graphics.  See Atronix R3/View repository.
;	make-host-ext.r		include/host-ext-graphics.h

	core-ext.r			include/host-ext-core.h

	make-host-init.r	include/host-init.h
	make-os-ext.r		include/host-lib.h
	make-reb-lib.r		include/reb-lib.h
]

;
; NOTE: In the following file lists, a (+) preceding a file is indicative that
; it is to be searched for comment blocks around the function prototypes
; that indicate the function is to be gathered to be put into the host-lib.h
; exports.  (This is similar to what make-headers.r does when it runs over
; the Rebol Core sources, except for the host.)
;

os: [
	host-main.c
	host-args.c
	+ host-device.c
	host-stdio.c
	host-core.c
	dev-net.c
	dev-dns.c
]

os-windows: [
	+ generic/host-memory.c

	+ windows/host-lib.c
	windows/dev-stdio.c
	windows/dev-file.c
	windows/dev-event.c
	windows/dev-clipboard.c
	windows/dev-serial.c
]

os-posix: [
	+ generic/host-memory.c
	+ generic/host-locale.c
	generic/iso-639.c
	generic/iso-3166.c
	+ generic/host-gob.c

	+ stub/host-encap.c

	posix/host-readline.c
	posix/dev-stdio.c
	posix/dev-event.c
	posix/dev-file.c

	+ posix/host-browse.c
	+ posix/host-config.c
	+ posix/host-error.c
	+ posix/host-library.c
	+ posix/host-process.c
	+ posix/host-thread.c
	+ posix/host-time.c
]

os-osx: [
	+ generic/host-memory.c
	+ generic/host-locale.c
	generic/iso-639.c
	generic/iso-3166.c
	+ generic/host-gob.c

	+ stub/host-encap.c

	; OSX uses the POSIX file I/O for now
	posix/host-readline.c
	posix/dev-stdio.c
	posix/dev-event.c
	posix/dev-file.c

	+ posix/host-browse.c
	+ posix/host-config.c
	+ posix/host-error.c
	+ posix/host-library.c
	+ posix/host-process.c
	+ posix/host-thread.c
	+ posix/host-time.c
]

; The Rebol open source build did not differentiate between linux and simply
; posix builds.  However Atronix R3/View uses a different `os-base` name.
; make-make.r requires an `os-(os-base)` entry here for each named target.
;
os-linux: [
	+ generic/host-memory.c
	+ generic/host-locale.c
	generic/iso-639.c
	generic/iso-3166.c
	+ generic/host-gob.c

	; Linux uses the POSIX file I/O for now
	posix/host-readline.c
	posix/dev-stdio.c
	posix/dev-file.c

	; It also uses POSIX for most host functions
	+ posix/host-config.c
	+ posix/host-error.c
	+ posix/host-library.c
	+ posix/host-process.c
	+ posix/host-thread.c
	+ posix/host-time.c

	; Linux has some kind of MIME-based opening vs. posix /usr/bin/open
	+ linux/host-browse.c

	; Atronix dev-event.c for linux depends on X11, and core builds should
	; not be using X11 as a dependency (probably)
	posix/dev-event.c

	; Linux has support for ELF format encapping
	+ linux/host-encap.c

	; There is a Linux serial device
	linux/dev-serial.c

	; Linux supports siginfo_t-style signals
	linux/dev-signal.c
]

boot-files: [
	version.r

; Ren/C is core sources with no graphics.  See Atronix R3/View repository.
;
;	graphics.r
;	draw.r
;	shape.r
;	text.r
]

mezz-files: [
; The old style prot-http.r seems to have been replaced, was commented out.
;
;	prot-http.r

; Ren/C is core sources with no graphics.  See Atronix R3/View repository.
;
;	view-colors.r
;	view-funcs.r
]

; Ren/C is core sources with no graphics.  See Atronix R3/View repository.
; (Additionally, Ren/C cannot have any .cpp files as a dependency...though
; it can build as C++ it should not require it)
;
;agg-files: [
;	agg_arc.cpp
;	agg_arrowhead.cpp
;	agg_bezier_arc.cpp
;	agg_bspline.cpp
;	agg_curves.cpp
;	agg_image_filters.cpp
;	agg_line_aa_basics.cpp
;	agg_path_storage.cpp
;	agg_rasterizer_scanline_aa.cpp
;	agg_rounded_rect.cpp
;	agg_sqrt_tables.cpp
;	agg_trans_affine.cpp
;	agg_trans_single_path.cpp
;	agg_vcgen_bspline.cpp
;	agg_vcgen_contour.cpp
;	agg_vcgen_dash.cpp
;	agg_vcgen_markers_term.cpp
;	agg_vcgen_smooth_poly1.cpp
;	agg_vcgen_stroke.cpp
;	agg_vpgen_segmentator.cpp
;	agg_compo.cpp
;	agg_graphics.cpp
;	agg_font_freetype.cpp
;	agg_font_win32_tt.cpp
;	agg_truetype_text.cpp
;	agg_effects.cpp
;	compositor.cpp
;	graphics.cpp
;	rich_text.cpp
;]

tools: [
	make-host-init.r
	make-host-ext.r
	form-header.r
]
