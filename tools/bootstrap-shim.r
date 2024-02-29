REBOL [
    Title: "Shim to bring old executables up to date to use for bootstrapping"
    Rights: {
        Rebol 3 Language Interpreter and Run-time Environment
        "Ren-C" branch @ https://github.com/metaeducation/ren-c

        Copyright 2012-2018 Rebol Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Purpose: {
        Ren-C "officially" supports two executables for doing a bootstrap
        build.  One is a frozen "stable" version (`8994d23`) which was
        committed circa Dec-2018:

        https://github.com/metaeducation/ren-c/commit/dcc4cd03796ba2a422310b535cf01d2d11e545af

        The only other executable that is guaranteed to work is the *current*
        build.  This is ensured by doing a two-step build in the continuous
        integration, where 8994d23 is used to make the first one, and then
        the build is started over using that product.

        This shim is for 8994d23, in order to bring it up to compatibility
        for any new features used in the bootstrap code that were introduced
        since it was created.  This is facilitated by Ren-C's compositional
        operations, like ADAPT, CHAIN, SPECIALIZE, and ENCLOSE.
    }
]

read: lib/read: adapt 'lib/read [
    ;
    ; !!! This can be useful in build 8994d23 to get better error messages
    ; about where a bad read is reading from (fixed in R3C and later)
    ;
    ; if not port? source [print ["READING:" mold source "from" what-dir]]
]


; New interpreters do not change the working directory when a script is
; executed to the directory of the script.  This is in line with what most
; other languages do.  Because this makes it more difficult to run helper
; scripts relative to the script's directory, `do <subdir/script.r>` is used
; with a TAG! to mean "run relative to system/script/header".
;
; To subvert the change-to-directory-of-script behavior, we have to LOAD the
; script and then DO it, so that DO does not receive a FILE!.  But this means
; we have to manually update the system/script object.
;
do: enclose :lib/do func [f <local> old-system-script] [
    old-system-script: _
    if tag? :f/source [
        f/source: append copy system/script/path to text! f/source
        f/source: clean-path f/source
        old-system-script: system/script
        system/script: construct system/standard/script [
            title: spaced ["Bootstrap Shim DO LOAD of:" f/source]
            header: compose [File: f/source]
            parent: system/script
            path: first split-path f/source
            args: f/args
        ]
        f/source: load f/source  ; avoid dir-changing mechanic of DO FILE!
    ]
    lib/do f  ; avoid SET/ANY vs SET/OPT problem by using ELIDE
    elide if old-system-script[
        system/script: old-system-script
    ]
]
load: func [source /all /header] [  ; can't ENCLOSE, does not take TAG!
    if tag? source [
        source: append copy system/script/path to text! source
    ]
    case [
        header [
            assert [not all]
            lib/load/header source
        ]
        all [lib/load/all source]
        true [lib/load source]
    ]
]

; The snapshotted Ren-C existed right before <blank> was legal to mark an
; argument as meaning a function returns null if that argument is blank.
; See if this causes an error, and if so assume it's the old Ren-C, not a
; new one...?
;
; What this really means is that we are only catering the shim code to the
; snapshot.  (It would be possible to rig up shim code for pretty much any
; specific other version if push came to shove, but it would be work for no
; obvious reward.)
;
trap [
    func [i [<blank> integer!]] [...]
] or [
    nulled?: func [var [word! path!]] [return null = get var]
    quit/with system/options/path
]

print "== SHIMMING OLDER R3 TO MODERN LANGUAGE DEFINITIONS =="

; Older Ren-C considers nulled variables to be "unset".
;
nulled?: :unset?

; The "real apply" hasn't really been designed, but it would be able to mix
; positional arguments with named ones.  This is changed by the nature of
; "refinements are their arguments" to empower more clean options.  What Ren-C
; originally called its APPLY is thus moved to the weird name APPLIQUE, that
; had previously been taken for compatibility apply (now REDBOL-APPLY)
;
applique: :apply
unset 'apply

collect*: :collect
collect: :collect-block

modernize-action: function [
    "Account for the <blank> annotation as a usermode feature"
    return: [block!]
    spec [block!]
    body [block!]
][
    blankers: copy []
    spec: collect [
        iterate spec [
            ;
            ; Find ANY-WORD!s (args/locals)
            ;
            if keep w: match any-word! spec/1 [
                ;
                ; Feed through any TEXT!s following the ANY-WORD!
                ;
                while [if (tail? spec: my next) [break] | text? spec/1] [
                    keep/only spec/1
                ]

                ; Substitute BLANK! for any <blank> found, and save some code
                ; to inject for that parameter to return null if it's blank
                ;
                if find (try match block! spec/1) <blank> [
                    keep/only replace copy spec/1 <blank> 'blank!
                    append blankers compose [
                        if blank? (as get-word! w) [return null]
                    ]
                    continue
                ]
            ]
            keep/only spec/1
        ]
    ]
    body: compose [
        (blankers)
        (as group! body)
    ]
    return reduce [spec body]
]

func: adapt 'func [set [spec body] modernize-action spec body]
function: adapt 'function [set [spec body] modernize-action spec body]

meth: enfix adapt 'meth [set [spec body] modernize-action spec body]
method: enfix adapt 'method [set [spec body] modernize-action spec body]

trim: adapt 'trim [ ; there's a bug in TRIM/AUTO in 8994d23
    if auto [
        while [not tail? series and [series/1 = LF]] [
            take series
        ]
    ]
]

join: :join-of  ; Note: JOIN now for strings and paths only (not arrays)

quit/with system/options/path
