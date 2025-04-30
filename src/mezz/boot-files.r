Rebol [
    system: "Rebol [R3] Language Interpreter and Run-time Environment"
    title: "REBOL 3 Boot: System Contexts"
    rights: --[
        Copyright 2012 REBOL Technologies
        REBOL is a trademark of REBOL Technologies
    ]--
    license: --[
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    ]--
    notes: "Used by %make/make-boot.r"
]

; const: basic constants (before %sysobj.r is initialized)
[
    %base-constants.r
]

; base: low-level boot in lib context:
[
    %base-defs.r
    %base-funcs.r
    %base-series.r
    %base-files.r
]

; sys: low-level sys context:
[
    %sys-base.r
    %sys-ports.r
    %sys-codec.r ; export to lib!
    %sys-load.r
]

; mezz: mid-level lib context:
[
    %mezz-types.r
    %mezz-debug.r
    %mezz-dump.r
    %mezz-control.r
    %mezz-save.r
    %mezz-series.r
    %mezz-files.r
    %mezz-shell.r
    %mezz-math.r
    %mezz-help.r  ; depends on DUMP-OBJ in %mezz-dump.r
    %mezz-colors.r
    %mezz-legacy.r

    %uparse.r  ; migrated to mezzanine, will someday have native portions
    %uparse-extras.r  ; e.g. DESTRUCTURE
]
