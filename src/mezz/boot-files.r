REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "REBOL 3 Boot: System Contexts"
    Rights: {
        Copyright 2012 REBOL Technologies
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Note: "Used by %make/make-boot.r"
]

;-- base: low-level boot in lib context:
[
    %base-constants.r
    %base-defs.r
    %base-funcs.r
    %base-infix.r
    %base-series.r
    %base-files.r
]

;-- sys: low-level sys context:
[
    %sys-base.r
    %sys-ports.r
    %sys-codec.r ; export to lib!
    %sys-load.r
    %sys-start.r
]

;-- lib: mid-level lib context:
[
    %mezz-types.r
    %mezz-func.r
    %mezz-debug.r
    %mezz-control.r
    %mezz-save.r
    %mezz-series.r
    %mezz-files.r
    %mezz-shell.r
    %mezz-math.r
    %mezz-help.r ; move dump-obj!
    %mezz-colors.r
    %mezz-tail.r
    %mezz-legacy.r
]

;-- protocols:
[
    ;moved to file-base and loaded by host-start.r
    ;%prot-http.r
    ;%prot-tls.r
]
