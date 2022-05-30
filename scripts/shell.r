REBOL [
    System: "Ren-C Interpreter and Run-time Environment"
    Title: "OS Shell Interaction Dialect"
    Type: module
    Name: Shell-Dialect
    Rights: {
        Copyright 2015-2020 hostilefork.com
        Copyright 2020 Ren-C Open Source Contributors

        See README.md and CREDITS.md for more information.
    }
    License: {LGPL 3.0}
    History: {
        SHELL originated in the Ren Garden UI Experiment:
        https://youtu.be/0exDvv5WEv4?t=553
    }
    Description: {
        The premise of this dialect is to make it possible to write shell
        commands in as natural a way as possible.  This means that WORD!s
        act as their literal spellings...needing GROUP!s to run Ren-C code
        or fetch Ren-C variables:

            >> extension: "txt"
            >> shell/inspect [ls -alF *.(extension)]
            == {ls -alF *.txt}

        TEXT! strings will literally include their quotes, and GROUP!s imply
        quotation as well.  In order to "splice in" a TEXT! without putting
        it in quotes, use a GET-GROUP!

            >> command: "ls -alF"
            >> shell/inspect [(command)]
            == {"ls -alF"}

            >> command "ls -alF"
            >> shell [:(command)]
            == {ls -alF}

        For a literal form that does not escape with quotes, ISSUE! may be
        used.  Hence `#"foo bar"` acts the same as `:("foo bar")`.
    }
    Notes: {
        * A disadvantage of this implementation (compared to the Ren Garden
          implementation) is that each call to SHELL forgets the environment
          variable settings from the previous call.  If the CALL process could
          be held open (e.g. as a PORT!) then this could be addressed.  That
          is something that is definitely desired.
    }
]


%%: func [ ; %make-file.r shared with bootstrap, can't load %%
    {Quoting MAKE FILE! Operator}
    'value [word! path! tuple! block! group!]
][
    if group? value [value: eval value]
    make-file value
]


shell: func [
    {Run code in the shell dialect}
    return: [text!]
    code "Dialected shell code"
        [block!]
    /pipe
    /inspect "Return the shell command as TEXT!"
][
    ; NOTE: We don't use GET-ENV to fill in <ENV> variables, because this
    ; code runs before the CALL and wouldn't pick up changes to the
    ; environment.
    ;
    let shellify-block: func [block [block!]] [
        if 1 <> length of block [
            fail ["SHELL expects BLOCK!s to have one item:" mold block]
        ]

        let item: first block
        if group? item [item: do item]

        return switch type of item [
            text! word! [unspaced ["${" item "}"]]

            fail ["SHELL expects [ENV] blocks to be WORD! or TEXT!:" mold item]
        ]
    ]

    ; TAG! is treated as an environment variable lookup.  We don't use the
    ; GET-ENV function here, because we haven't run the shell code yet...and
    ; the environment might change by the time it is reached.
    ;
    let shellify-tag: func [value [any-value!]] [
        if not tag? value [return value]

        if system.version.4 = 3 [   ; Windows
            unspaced ["%" as text! value "%"]
        ] else [
            unspaced ["${" as text! value "}"]
        ]
    ]

    ; The MAKE-FILE logic is targeting being built into the system, so it is
    ; not intended to be connected to things like environment variables.
    ; But we want to be able to substitute environment variables as parts of
    ; the expressions.
    ;
    let process-tag: func [container [path! tuple! block!]] [
        to type-of-container map-each item container [
            if group? item [
                item: do item
            ]

            ensure/not tag! item [shellify-tag item]
        ]
    ]

    let command: spaced collect [for-next pos code [
        while [new-line? pos] [
            if pos.1 = '... [
                pos: next pos  ; skip, don't output new-line
                continue
            ]
            keep newline
            break
        ]

        let item: :pos.1

        ; The default behaviors for each type may either splice or not.
        ; But when you use a GROUP! or a BLOCK!, it will put things in quotes.
        ; To bypass this behavior, use GET-GROUP! or GET-BLOCK!

        let splice: <default>
        switch type of item [
            group! [splice: false, item: try do item]

            get-group! [splice: true, item: try do item]
            get-block! [splice: true, item: as block! item]
        ]
        let needs-quotes?: func [return: [logic!] item] [
            if match [word! issue!] item [return false]  ; never quoted
            if file? item [
                return did find item space  ; !!! should check other escapes
            ]
            if splice = false [return true]
            if splice = true [return false]  ; e.g. even TEXT! has no quotes
            return text? item  ; plain `$ ls "/foo"` puts quotes on "/foo"
        ]

        item: switch type of item [
            blank! [continue]  ; !!! should you have to use #_ for undercore?

            integer! decimal! [form item]

            word! issue! [item]  ; never quoted or escaped

            text! [replace/all copy item {"} {\"}]  ; sometimes spliced

            file! [item]

            tag! [shellify-tag item]

            ; !!! For the moment, slashes are left in the forward direction
            ; in paths.  They are converted to text without quotes, and with
            ; variable substitutions via TAG!.  This means Windows doesn't
            ; get the variable substitution feature unless an operator like %%
            ; were to be used.
            ;
            path! tuple! block! [
                as issue! make-file/predicate item :shellify-tag
            ]

            fail ["SHELL doesn't know what this means:" mold item]
        ]

        if needs-quotes? item [
            if file? item [item: file-to-local item]
            keep unspaced [{"} form item {"}]  ; !!! better escape for strings
        ] else [
            if file? item [item: file-to-local item]
            keep form item
        ]
    ]]

    if inspect [
        return command
    ]

    if not command [return null]  ; SPACED components all vaporized

    if not pipe [
        lib.call/shell command  ; must use LIB (binding issue)
        return  ; don't show any result in console
    ]

    let output: copy ""
    lib.call/shell/output command output  ; must use LIB (binding issue)
    return output
]


$: func [
    {Run SHELL code to end of line (or continue on next line with `...`)}
    :args "See documentation for SHELL"
        [any-value! <variadic>]
    /inspect
    /pipe
    <local> code item
][
    code: collect [
        cycle [
            ; We pass the ... through to the shell dialect, which knows how
            ; to handle it as a line continuation.
            ;
            all [new-line? args, '... <> first args] then [break]

            keep take args else [break]
        ]
    ]

    apply :shell [code, /inspect inspect, /pipe pipe]
]

export [shell %% $]
