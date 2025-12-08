Rebol [
    system: "Rebol [R3] Language Interpreter and Run-time Environment"
    title: "Source File Database"

    rights: --[
        Copyright 2012 REBOL Technologies
        Copyright 2012-2025 Rebol Open Source Contributos
        REBOL is a trademark of REBOL Technologies
    ]--

    license: --[
        Licensed under the Lesser GPL, Version 3.0 (the "License");
        See: https://www.gnu.org/licenses/lgpl-3.0.html
    ]--

    purpose: --[
        Lists of files (and compiler switches) used for creating makefiles.
    ]--

    notes: --[
     A. The bootstrap executable lacks generic TUPLE!.  So for compatibility
        it loads things like (obj.member) as the PATH! (obj/member).  That
        means things like [a-constants.c] will load as [a-constants/c]...we
        have to compensate for that in the build process, but it's worth it
        to not have to use [%a-constants.c]

        (This convention of not using FILE! for succinctness originated in
        R3-Alpha, so it isn't some strange Ren-C-ism.)

     B. Evaluator files use `#prefer-O2-optimization`.  There are good reasons
        to optimize the evaluator itself even if one is doing a "size-biased"
        build.  (At one time it also cut down on recursions, critical before
        the stackless trampoline because the WASM build would easily run out
        of call stack in the browser.)

     C. %f-dtoa.c comes from a third party and is an old file.  There is an
        updated package, but it is not a single-file...rather something with
        a complex build process.  If it were to be updated, it would need
        to be done through a process that extracted it in a way to fit into
        the ethos of the Rebol build process.  Hence we add tolerance for
        the many warnings that the file has.

     D. Zlib is an active project so it would be worth it to check to see
        if minor patches for subverting Spectre mitigation would be taken.
    ]--
]

core: [
    api/ -> [
        a-constants.c
        a-globals.c  ; !!! Why is this an a-xxx.c file?
        a-lib.c
    ]

    boot/ -> [
        b-init.c
    ]

    functionals/ -> [  ; Function Generators
        c-adapt.c
        c-augment.c
        c-arrow.c
        c-chain.c
        c-does.c
        c-enclose.c
        n-function.c
        c-hijack.c
        c-lambda.c
        c-macro.c
        c-native.c
        c-oneshot.c
        c-reframer.c
        c-reorder.c
        c-specialize.c
        c-typechecker.c
        c-yielder.c
    ]

    ; (C)ore (?)
    c-bind.c
    c-do.c
    c-context.c
    c-error.c

    c-function.c
    c-path.c
    c-signal.c
    c-state.c
    c-value.c
    c-word.c

    diagnostics/ -> [
        d-backtrace.c [
            <msc:/wd4668>  ; undefined winioctl.h defines, defaulting to 0
        ]
        d-crash.c
        d-dump.c
        d-eval.c
        d-gc.c
        d-print.c
        d-stack.c
        d-stats.c
        d-test.c
        d-trace.c
    ]

    evaluator/ -> [  ; use #prefer-O2-optimization, see [B]
        c-eval.c [#prefer-O2-optimization]
        c-step.c [#prefer-O2-optimization]
        c-action.c [#prefer-O2-optimization]
        c-trampoline.c [#prefer-O2-optimization]
    ]

    ; (F)???
    f-blocks.c
    f-dtoa.c [ ; old third-party file, see [C]
        <msc:/wd5045>  ; https://stackoverflow.com/q/50399940
        <msc:/wd4146>  ; unary minus operator applied to unsigned type

        <msc:/analyze->  ; explicitly don't static analyze

        <gcc:-Wno-cast-qual>  ; e.g. `*sp = (char*)s0 - 1;`
        <gcc:-Wno-unused-const-variable>  ; e.g. `tinytens`, `bigtens`, `tens`

        <no-sign-compare>
        <no-uninitialized>
        <implicit-fallthru>
    ]
    f-enbase.c [
        ; At time of writing there are 4 Spectre mitigations, which should
        ; be looked at and rewritten when there is time:
        ;
        <msc:/wd5045>  ; https://stackoverflow.com/q/50399940
    ]
    f-extension.c
    f-int.c
    f-math.c
    f-modify.c
    f-qsort.c [<gcc:-Wno-null-pointer-subtraction>]
    f-random.c
    f-round.c
    f-series.c
    f-stubs.c

    lexer/ -> [
        l-scan.c
        l-types.c
    ]

    memory/ -> [
        m-gc.c
        m-pools.c [<no-uninitialized>]
        m-series.c
        m-stacks.c
    ]

    natives/ -> [
        n-bitwise.c
        n-compose.c
        n-control.c
        n-data.c
        n-do.c
        n-error.c
        n-get-set.c
        n-ghost.c
        n-io.c
        n-loop.c
        n-make.c
        n-math.c
        n-port.c
        n-protect.c
        n-reduce.c
        n-series.c
        n-sets.c
        n-strings.c
        n-system.c
        n-transcode.c
        n-tweak.c
    ]

    parse/ -> [
        c-combinator.c
        n-parse3.c
    ]

    ; (S)trings
    s-cases.c
    s-crc.c
    s-find.c
    s-make.c
    s-mold.c
    s-ops.c

    types/ -> [
        t-binary.c
        t-bitset.c
        t-blank.c
        t-block.c
        t-char.c
        t-comma.c
        t-datatype.c
        t-date.c
        t-decimal.c
        t-integer.c
        t-logic.c
        t-map.c
        t-object.c
        t-pair.c
        t-port.c
        t-quoted.c
        t-string.c
        t-time.c
        t-tuple.c
        t-typeset.c
        t-word.c
        t-varargs.c
    ]

    ; (U)??? (3rd-party code extractions)
    u-compress.c
    u-zlib.c [  ; 3rd party-file, use mitigations, see [D]
        <no-make-header>
        <implicit-fallthru>
        <no-constant-conditional>

        <gcc:-Wno-unused-const-variable>  ; e.g. z_deflate_copyright

        <clang:-Wno-tautological-type-limit-compare>
        <clang:-Wno-strict-prototypes>

        <msc:/wd5045>  ; https://stackoverflow.com/q/50399940

        <msc:/analyze->  ; same, for static analysis fixes
    ]
]

; Files created by the make-boot process
;
generated: [
    %core/tmp-boot-block.c
    %core/tmp-typesets.c
    %core/tmp-builtin-extension-table.c
    %core/tmp-rebol-api-table.c
    %core/tmp-generic-tables.c

    %main/tmp-main-startup.c
]

main: 'main.c

boot-files: [
    version.r
]
