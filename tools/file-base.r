REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Source File Database"
    Rights: --{
        Copyright 2012 REBOL Technologies
        Copyright 2012-2017 Rebol Open Source Contributos
        REBOL is a trademark of REBOL Technologies
    }--
    License: --{
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }--
    Purpose: --{
        Lists of files used for creating makefiles.
    }--
]

; !!! Originally this table had files in it as TUPLE!, e.g. `a-constants.c`
; without the need for a % to decorate it.  That works in modern Ren-C, but
; the bootstrap executable was tweaked so that loads as `a-constants/c` in
; order to allow things that look like TUPLE! to build in bootstrap (which
; does not have generic tuple).  That out-prioritizes the nice look here of
; a tuple, but after bootstrapping to a modern EXE it could be used again.
; (There is however a hack which only turns dots into slashes until the
; first actual slash is seen, allowing paths like `functionals/c-adapt.c`
; to work without the % annotation)
;
core: [
    ; (A)???
    %a-constants.c
    %a-globals.c
    %a-lib.c

    ; (B)oot
    %b-init.c

    ; Function Generators
    ;
    functionals/c-adapt.c
    functionals/c-augment.c
    functionals/c-arrow.c
    functionals/c-chain.c
    functionals/c-combinator.c
    functionals/c-does.c
    functionals/c-enclose.c
    functionals/n-function.c
    functionals/c-hijack.c
    functionals/c-lambda.c
    functionals/c-macro.c
    functionals/c-native.c
    functionals/c-oneshot.c
    functionals/c-reframer.c
    functionals/c-reorder.c
    functionals/c-specialize.c
    functionals/c-typechecker.c
    functionals/c-yielder.c

    ; (C)ore
    %c-bind.c
    %c-do.c
    %c-context.c
    %c-error.c

    ; EVALUATOR
    ;
    ; Uses `#prefer-O2-optimization`.  There are several good reasons to
    ; optimize the evaluator itself even if one is doing a "size-biased"
    ; build.  (At one time it also cut down on recursions, critical before
    ; the stackless trampoline because the WASM build would easily run out
    ; of call stack in the browser.)
    [
        evaluator/c-eval.c  #prefer-O2-optimization
    ][
        evaluator/c-step.c  #prefer-O2-optimization
    ][
        evaluator/c-action.c  #prefer-O2-optimization
    ][
        evaluator/c-trampoline.c  #prefer-O2-optimization
    ]

    %c-function.c
    %c-path.c
    %c-port.c
    %c-signal.c
    %c-state.c
    %c-value.c
    %c-word.c

    ; (D)ebug
    %d-crash.c
    %d-dump.c
    %d-eval.c
    %d-gc.c
    %d-print.c
    %d-stack.c
    %d-stats.c
    %d-test.c
    %d-trace.c

    ; (F)???
    %f-blocks.c
    [
        %f-deci.c

        ; May 2018 update to MSVC 2017 added warnings for Spectre mitigation.
        ; %f-deci.c is a lot of twiddly custom C code for implementing a fixed
        ; precision math type, that was for some reason a priority in R3-Alpha
        ; but isn't very central to Ren-C.  It is not a priority to audit
        ; it for speed, so allow it to be slow if MSVC compiles with /Qspectre
        ;
        <msc:/wd5045>  ; https://stackoverflow.com/q/50399940
    ]
    [
        %f-dtoa.c

        ; f-dtoa.c comes from a third party and is an old file.  There is an
        ; updated package, but it is not a single-file...rather something with
        ; a complex build process.  If it were to be updated, it would need
        ; to be done through a process that extracted it in a way to fit into
        ; the ethos of the Rebol build process.
        ;
        ; Hence we add tolerance for warnings that the file has.
        ;
        <msc:/wd5045>  ; https://stackoverflow.com/q/50399940
        <msc:/wd4146>  ; unary minus operator applied to unsigned type

        <msc:/analyze->  ; explicitly don't static analyze

        <gnu:-Wno-cast-qual>  ; e.g. `*sp = (char*)s0 - 1;`
        <gnu:-Wno-unused-const-variable>  ; e.g. `tinytens`, `bigtens`, `tens`

        <no-sign-compare>
        <no-uninitialized>
        <implicit-fallthru>
    ]
    [
        %f-enbase.c

        ; At time of writing there are 4 Spectre mitigations, which should
        ; be looked at and rewritten when there is time:
        ;
        <msc:/wd5045>  ; https://stackoverflow.com/q/50399940
    ]
    %f-extension.c
    %f-int.c
    %f-math.c
    %f-modify.c
    %f-qsort.c
    %f-random.c
    %f-round.c
    %f-series.c
    %f-stubs.c

    ; (L)exer
    %l-scan.c
    %l-types.c

    ; (M)emory
    %m-gc.c
    [%m-pools.c <no-uninitialized>]
    %m-series.c
    %m-stacks.c

    ; (N)atives
    %n-bitwise.c
    %n-control.c
    %n-data.c
    %n-do.c
    %n-error.c
    %n-get-set.c
    %n-io.c
    %n-loop.c
    %n-make.c
    %n-math.c
    %n-parse3.c
    %n-port.c
    %n-protect.c
    %n-reduce.c
    %n-series.c
    %n-sets.c
    %n-strings.c
    %n-system.c

    ; (S)trings
    %s-cases.c
    %s-crc.c
    %s-find.c
    %s-make.c
    %s-mold.c
    %s-ops.c

    ; (T)ypes
    %t-binary.c
    %t-bitset.c
    %t-blank.c
    %t-block.c
    %t-char.c
    %t-comma.c
    %t-datatype.c
    %t-date.c
    %t-decimal.c
    %t-integer.c
    %t-logic.c
    %t-map.c
    %t-money.c
    %t-object.c
    %t-pair.c
    %t-port.c
    %t-quoted.c
    %t-string.c
    %t-time.c
    %t-tuple.c
    %t-typeset.c
    %t-word.c
    %t-varargs.c

    ; (U)??? (3rd-party code extractions)
    %u-compress.c
    [
        %u-zlib.c

        <no-make-header>
        <implicit-fallthru>
        <no-constant-conditional>

        <gnu:-Wno-unused-const-variable>  ; e.g. z_deflate_copyright

        ; Zlib is an active project so it would be worth it to check to see
        ; if minor patches for subverting Spectre mitigation would be taken.
        ;
        <msc:/wd5045>  ; https://stackoverflow.com/q/50399940

        <msc:/analyze->  ; same, for static analysis fixes
    ]
]

; Files created by the make-boot process
;
generated: [
    %tmp-boot-block.c
    %tmp-type-hooks.c
    %tmp-typesets.c
    %tmp-builtin-extension-table.c
    %tmp-rebol-api-table.c
]

made: [
    %make-boot.r                 core/tmp-boot-block.c
    %make-extension-table.r      core/tmp-builtin-extension-table.c
    %make-librebol.r             core/tmp-rebol-api-table.c

    %make-headers.r              include/tmp-internals.h

    %make-host-init.r            include/host-init.h
    %make-librebol.r             include/rebol.h
]

main: %main.c

boot-files: [
    %version.r
]

mezz-files: [
    ; There were some of these in the R3/View build
]

tools: [
    %make-host-init.r
    %make-host-ext.r
]
