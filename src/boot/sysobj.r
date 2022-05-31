REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "System object"
    Rights: {
        Copyright 2012 REBOL Technologies
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0.
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Purpose: {
        Defines the system object. This is a special block that is evaluted
        such that its words do not get put into the current context.
    }
    Note: "Remove older/unused fields before beta release"
]

; Next five fields are updated during build:
version:  0.0.0
build:    1
platform: _
commit: _

product: _  ; assigned by startup of the host ('core, 'view, 'ren-garden...)

license: {Copyright 2012 REBOL Technologies
REBOL is a trademark of REBOL Technologies
Licensed under the Apache License, Version 2.0.
See: http://www.apache.org/licenses/LICENSE-2.0
}

catalog: make object! [
    ;
    ; These catalogs are filled in by Init_System_Object()
    ;
    datatypes: _
    actions: _
    natives: _
    errors: _
]

contexts: make object! [
    root:
    sys:
    lib:
    user:
        _
]

state: make object! [
    ; Mutable system state variables
    note: "contains protected hidden fields"
    policies: make object! [  ; Security policies
        file:    ; file access
        net:     ; network access
        eval:    ; evaluation limit
        memory:  ; memory limit
        protect: ; protect function
        debug:   ; debugging features
        envr:    ; read/write
        call:    ; execute only
        browse:  ; execute only
            0.0.0
        extension: 2.2.2 ; execute only
    ]
    last-error: _ ; used by WHY?
]

modules: []
extensions: []

codecs: make object! []

schemes: make object! []

ports: make object! [
    wait-list: []   ; List of ports to add to 'wait
    input:          ; Port for user input.
    output:         ; Port for user output
    system:         ; Port for system events
    callback: _     ; Port for callback events
;   serial: _       ; serial device name block
]

locale: make object! [
    language:   ; Human language locale
    language*: _
    library: make object! [
        ;
        ; This is a list mapping tags to URLs as [<tag> http://example.com]
        ; They make it easy to do things like `import <json>` or `do <chess>`
        ;
        utilities: https://raw.githubusercontent.com/r3n/renclib/master/userutils.reb
    ]
    locale:
    locale*: _
    months: [
        "January" "February" "March" "April" "May" "June"
        "July" "August" "September" "October" "November" "December"
    ]
    days: [
        "Monday" "Tuesday" "Wednesday" "Thursday" "Friday" "Saturday" "Sunday"
    ]
]

options: make object! [  ; Options supplied to REBOL during startup
    bin: '          ; Path to directory where Rebol executable binary lives
    boot: '         ; Path of executable, ie. system.options.bin/r3-exe
    home: '         ; Path of home directory
    resources: '    ; users resources directory (for %user.r, skins, modules etc)
    suppress: _     ; block of user --suppress items, eg [%rebol.r %user.r %console-skin.reb]
    loaded: []      ; block with full paths to loaded start-up scripts
    path: '         ; Where script was started or the startup dir

    current-path: ' ; Current URL! or FILE! path to use for relative lookups

    encap: '        ; The encapping data extracted
    script: '       ; Filename of script to evaluate
    args: '         ; Command line arguments passed to script
    debug: '        ; debug flags
    version: '      ; script version needed

    dump-size: 68   ; used by dump

    quiet: false    ; do not show startup info (compatibility)
    about: false    ; do not show full banner (about) on start-up
    cgi: false
    no-window: false
    verbose: false

    binary-base: 16    ; Default base for FORMed binary values (64, 16, 2)
    decimal-digits: 15 ; Max number of decimal digits to print.
    module-paths: [%./]
    default-suffix: %.reb ; Used by IMPORT if no suffix is provided
    file-types: copy [
        %.reb %.r3 %.r rebol
    ]

    ; Historical Rebol used PATH! for accessing members as well as refinements
    ; on functions.  Ren-C generalized TUPLE! for member access, and uses
    ; PATH! only for invoking functions.  If this flag is set to true, then
    ; it will emulate legacy behavior--though it is measurably slower.  :-(
    ; Ideally keep this to TRUE and use the new conventions.
    ;
    redbol-paths: false
]

script: make object! [
    title:          ; Title string of script
    header:         ; Script header as evaluated
    parent:         ; Script that loaded the current one
    path:           ; Location of the script being evaluated
    args:           ; args passed to script
        _
]

standard: make object! [
    ; FUNC implements a native-optimized variant of an action generator.
    ; This is the body template that it provides as the code *equivalent* of
    ; what it is doing (via a more specialized/internal method).  Though
    ; the only "real" body stored and used is the one the user provided
    ; (substituted in #BODY), this template is used to "lie" when asked what
    ; the BODY-OF the function is.
    ;
    ; The substitution location is hardcoded at index 5.  It does not "scan"
    ; to find #BODY, just asserts the position is an ISSUE!.

    func-body: [
        return: make action! [
            [{Returns a value from an action} value [<opt> <end> any-value!]]
            [unwind/with (binding of 'return) either end? 'value [] [:value]]
        ] #BODY
    ]

    proc-return-type: []  ; was once [bad-word!], now just []

    elider-return-type: [<void>]

    proc-body: [
        return: make action! [
            [{Returns a value from an action} value [<opt> <end> any-value!]]
            [unwind/with (binding of 'return) either end? 'value [] [:value]]
        ] #BODY
        void
    ]

    ; !!! The PORT! and actor code is deprecated, but this bridges it so
    ; it doesn't have to build a spec by hand.
    ;
    port-actor-spec: [port-actor-parameter [<opt> any-value!]]

    ; !!! The %sysobj.r initialization currently runs natives (notably the
    ; natives for making objects, and here using COMMENT because it can).
    ; This means that if the ACTION-META information is going to be produced
    ; from a spec block for natives, it wouldn't be available while the
    ; natives are getting initialized.
    ;
    ; It may be desirable to sort out this dependency by using a construction
    ; syntax and making this a MAP! or OBJECT! literal.  In the meantime,
    ; the archetypal context has to be created "by hand" for natives to use,
    ; with this archetype used by the REDESCRIBE Mezzanine.
    ;
    action-meta: make object! [
        description: '
        parameter-types: '
        parameter-notes: '
    ]

    ; !!! This is the template used for all errors, to which extra fields are
    ; added if the error has parameters.  It likely makes sense to put this
    ; information into the META-OF of the error, so that parameterizing the
    ; error does not require a keylist expansion...and also so that fields
    ; like FILE and LINE would not conflict with parameters.
    ;
    error: make object! [
        type: '
        id: '
        message: '  ; a BLOCK! template with arg substitution or just a STRING!
        near: '
        where: '
        file: '
        line: '

        ; Arguments will be allocated in the context at creation time if
        ; necessary (errors with no arguments will just have a message)
    ]

    script: make object! [
        title:
        header:
        parent:
        path:
        args:
            _
    ]

    ; Having a "standard header object" for modules/scripts means:
    ;
    ; (1) C code processing the module can take for granted the order in which
    ;     the standard fields are, potentially processing it faster.
    ;
    ; (2) So long as you don't use any additional fields, the list of keys
    ;     could be shared between objects.
    ;
    ; (3) You can provide default values more easily.
    ;
    ; This is the existing resource for knowing what historical keys were:
    ;
    ;   http://www.rebol.org/one-click-submission-help.r
    ;   https://forum.rebol.info/t/1674
    ;
    ; !!! Historically headers use titlecase keys.  In the current world, that
    ; leads to a difference from if you use lowercase ones.
    ;
    ; !!! We are using MAKE OBJECT! here which allows NULL variables via
    ; evaluation.  But ordinarily the headers can't do that because they
    ; are blocks and not evaluated.  This is developing, but the general
    ; gist here is that when round-tripping headers to blocks the NULL fields
    ; are effectively absent for the semantics of *this* object.
    ;
    header: make object! [
        Title: {Untitled}
        File: '
        Name: '
        Type: 'script  ; !!! Is this a good default?
        Version: '
        Date: '
        Author: '
        Options: '
        Description: '

        ; !!! `Compress:`, `Exports:`, and `Contents:` were commented out.
        ; Exports perhaps because they only applied to modules, and would be
        ; misleading on a plain script.  The other perhaps because they were
        ; not processed by C code so (1) did not apply.  `Needs:` has been
        ; removed while focusing on the IMPORT dialect design.  `Checksum:`
        ; was a basically useless feature, as there's no security benefit to
        ; having the checksum of the body travel in the same file as a fully
        ; tamperable header.
    ]

    scheme: make object! [
        name:       ; word of http, ftp, sound, etc.
        title:      ; user-friendly title for the scheme
        spec:       ; custom spec for scheme (if needed)
        info:       ; prototype info object returned from query
;       kind:       ; network, file, driver
;       type:       ; bytes, integers, objects, values, block
        actor:      ; standard action handler for scheme port functions
            _
    ]

    port: make object! [ ; Port specification object
        spec: '     ; published specification of the port
        scheme: '   ; scheme object used for this port
        actor: '    ; port action handler (script driven)

        ; !!! Native ports typically used raw C structs stored in a BINARY!
        ; as the `state`.  This makes that state opaque to the garbage
        ; collector, so it is a problem if REBVAL*/REBSER* are stored in it.
        ;
        state: '    ; internal state values (private)

        data: '     ; data buffer (usually binary or block)
        locals: '   ; user-defined storage of local data

        ; !!! With asynchronous events, TRAP cannot be used, e.g. you can't
        ; say `trap [write...]` if the error will happen outside of the
        ; write.  What happens instead is that the error is poked into this
        ; field of the port, and an 'error EVENT! is raised.  This falls into
        ; the general problem of R3-Alpha's model which is that it's not tied
        ; to any particular request...just a field on the port, so you don't
        ; know *what* errored.  A more sensible approach (like callback
        ; functions) could pass the error to the callback as every other such
        ; language would do.
        ;
        error: '
    ]

    port-spec-head: make object! [
        title:      ; user-friendly title for port
        scheme:     ; reference to scheme that defines this port
        ref:        ; reference path or url (for errors)
        path:       ; used for files
           _            ; (extended here)
    ]

    port-spec-net: make port-spec-head [
        host: _
        port-id: 80

        ; Set this to make outgoing packets seem to originate from a specific
        ; port (it's done by calling bind() before the first sendto(),
        ; otherwise the OS will pick an available port and stick with it.)
        ;
        local-id: _

        ; This should be set to a function that takes a PORT! on listening
        ; sockets...it will be called when a new connection is made.
        ;
        accept: '
    ]

    port-spec-signal: make port-spec-head [
        mask: [all]
    ]

    file-info: make object! [
        name:
        size:
        date:
        type:
            _
    ]

    net-info: make object! [
        local-ip:
        local-port:
        remote-ip:
        remote-port:
            _
    ]

    ; !!! "Type specs" were an unfinished R3-Alpha concept, that when you said
    ; SPEC-OF INTEGER! or similar, you would not just get a textual name for
    ; it but optionally other information (like numeric limits).  The gist is
    ; reasonable, though having arbitrary precision integers is more useful.
    ; Since the feature was never developed, Ren-C merged the %typespec.r
    ; descriptions into the %types.r for easier maintenance.  So all that's
    ; left is the name, but an object is synthesized on SPEC OF requests just
    ; as a placeholder to remember the idea.
    ;
    type-spec: make object! [
        title: _
    ]

    utype: _
    font: _  ; mezz-graphics.h
    para: _  ; mezz-graphics.h
]


user: make object! [
   name:           ; User's name
   home:           ; The HOME environment variable
   words: _
   identity: make object! [email: smtp: pop3: esmtp-user: esmtp-pass: fqdn: _]
   identities: []
]

console: _  ; console (repl) object created by the console extension


cgi: make object! [ ; CGI environment variables
       server-software:
       server-name:
       gateway-interface:
       server-protocol:
       server-port:
       request-method:
       path-info:
       path-translated:
       script-name:
       query-string:
       remote-host:
       remote-addr:
       auth-type:
       remote-user:
       remote-ident:
       Content-Type:           ; cap'd for email header
       content-length: _
       other-headers: []
]

; Boot process does a sanity check that this evaluation ends with ~done~
~done~
