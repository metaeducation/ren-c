Rebol [
    system: "Rebol [R3] Language Interpreter and Run-time Environment"
    title: "System object"
    rights: --[
        Copyright 2012 REBOL Technologies
        REBOL is a trademark of REBOL Technologies
    ]--
    license: --[
        Licensed under the Apache License, Version 2.0.
        See: http://www.apache.org/licenses/LICENSE-2.0
    ]--
    purpose: --[
        Defines the system object. This is a special block that is evaluted
        such that its words do not get put into the current context.
    ]--
    notes: "Remove older/unused fields before beta release"
]

; Next five fields are updated during build:
version:  0.0.0
build:    1
platform: null
commit: null

product: null  ; assigned by startup of the host ('core, 'view, 'ren-garden...)

license: --[Copyright 2012 REBOL Technologies
Copyright 2012-2024 Ren-C Open Source Contributors
REBOL is a trademark of REBOL Technologies

Licensed under the Lesser GPL, Version 3.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at
https://www.gnu.org/licenses/lgpl-3.0.html
]--

catalog: construct [
    ;
    ; !!! R3-Alpha had several "catalogs" (natives, errors, datatypes, etc.)
    ; These were just lists.  They served no obvious purpose, however there
    ; was a purpose--which was to keep them alive from garbage collection.
    ;
    ; !!! Natives in LIB need to be locked from modification "except by people
    ; who really know what they're doing" (and replace the native with
    ; something compatible that doesn't crash when used as LIB(NATIVE_NAME)).
    ;
    ; !!! Datatypes now have their own module (which LIB inherits) that can
    ; be used as a "catalog" and also keeps them live from GC.
    ;
    ; !!! Errors catalog is all that is left.  Review.
    ;
    errors: null
]

contexts: construct [
    datatypes:
    lib:
    user:
        null
]

state: construct [
    ; Mutable system state variables
    note: "contains protected hidden fields"
    policies: construct [  ; !!! was for removed SECURE code
        file:    ; file access
        net:     ; network access
        eval:    ; evaluation limit
        memory:  ; memory limit
        protect: ; protect function
        debug:   ; debugging features
        envr:    ; read and write
        call:    ; execute only
        browse:  ; execute only
            0.0.0
        extension: 2.2.2 ; execute only
    ]
    last-error: null ; used by WHY?
]

modules: '[]
extensions: '[]

codecs: construct []

schemes: construct []

util: null

ports: construct [
    wait-list: '[]  ; List of ports to add to 'wait
    input:          ; Port for user input.
    output:         ; Port for user output
    system:         ; Port for system events
    callback: null  ; Port for callback events
;   serial: null    ; serial device name block
]

locale: construct [
    language:   ; Human language locale
    language*: null
    library: construct [
        ;
        ; This is a list mapping tags to URLs as [<tag> http://example.com]
        ; They make it easy to do things like `import @json` or `do @chess`
        ;
        utilities: https://raw.githubusercontent.com/r3n/renclib/master/userutils.reb
    ]
    locale:
    locale*: null
    months: '[
        "January" "February" "March" "April" "May" "June"
        "July" "August" "September" "October" "November" "December"
    ]
    days: '[
        "Monday" "Tuesday" "Wednesday" "Thursday" "Friday" "Saturday" "Sunday"
    ]
]

options: construct [  ; Options supplied to REBOL during startup
    bin: null       ; Path to directory where Rebol executable binary lives
    boot: null      ; Path of executable, ie. system.options.bin/r3-exe
    home: null      ; Path of home directory
    resources: null ; users resources directory (for %user.r, skins, modules etc)
    suppress: null  ; block of user --suppress items, eg [%rebol.r %user.r %console-skin.reb]
    loaded: '[]     ; block with full paths to loaded start-up scripts
    path: null      ; Where script was started or the startup dir

    current-path: null  ; Current URL! or FILE! path to use for relative lookups

    encap: null     ; The encapping data extracted
    script: null    ; Filename of script to evaluate
    args: null      ; Command line arguments passed to script
    debug: null     ; debug flags
    version: null   ; script version needed

    dump-size: 68   ; used by dump

    quiet: 'no      ; do not show startup info (compatibility)
    about: 'no      ; do not show full banner (about) on start-up
    cgi: 'no
    verbose: 'no

    module-paths: '[%./]
    default-suffix: %.reb ; Used by IMPORT if no suffix is provided
    file-types: copy '[
        %.reb %.r3 %.r rebol
    ]
]

script: construct [
    title:          ; Title string of script
    header:         ; Script header as evaluated
    parent:         ; Script that loaded the current one
    path:           ; Location of the script being evaluated
    args:           ; args passed to script
        null
]

standard: make object! [  ; can't CONSTRUCT, dependency of MAKE on prior fields
    ;
    ; FUNC implements a native-optimized variant of an action generator.
    ; This is the body template that it provides as the code *equivalent* of
    ; what it is doing (via a more specialized/internal method).  Though
    ; the only "real" body stored and used is the one the user provided
    ; (substituted in #BODY), this template is used to "lie" when asked what
    ; the BODY-OF the function is.
    ;
    ; The substitution location is hardcoded at index 7.  It does not "scan"
    ; to find #BODY, just asserts the position is an ISSUE!.

    func-body: [
        /return: couple definitional-return/ binding of $return
        #BODY
        ~  ; if you don't call RETURN, the result is a ~ antiform (nothing)
    ]

    ; !!! The %sysobj.r initialization currently runs natives (notably the
    ; natives for making objects, and here using COMMENT because it can).
    ; This means that if the ACTION-ADJUNCT information is going to be produced
    ; from a spec block for natives, it wouldn't be available while the
    ; natives are getting initialized.
    ;
    ; It may be desirable to sort out this dependency by using a construction
    ; syntax and making this a MAP! or OBJECT! literal.  In the meantime,
    ; the archetypal context has to be created "by hand" for natives to use,
    ; with this archetype used by the REDESCRIBE Mezzanine.
    ;
    action-adjunct: construct [
        description: null
    ]

    ; !!! This is the template used for all errors, to which extra fields are
    ; added if the error has parameters.  It likely makes sense to put this
    ; information into the ADJUNCT-OF of the error, so that parameterizing the
    ; error does not require a keylist expansion...and also so that fields
    ; like FILE and LINE would not conflict with parameters.
    ;
    error: construct [
        type: null
        id: null
        message: null  ; BLOCK! template with arg substitution or just a STRING!
        near: null
        where: null
        file: null
        line: null

        ; Arguments will be allocated in the context at creation time if
        ; necessary (errors with no arguments will just have a message)
    ]

    script: construct [
        title:
        header:
        parent:
        path:
        args:
            null
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
    ; !!! We are using CONSTRUCT here which allows ~null~ antiforms via
    ; evaluation.  But ordinarily the headers can't do that because they
    ; are blocks and not evaluated.  This is developing, but the general
    ; gist here is that when round-tripping headers to blocks the NULL fields
    ; are effectively absent for the semantics of *this* object.
    ;
    header: construct [
        title: "Untitled"
        file: null
        name: null
        type: 'script  ; !!! Is this a good default?
        version: null
        date: null
        author: null
        options: null
        description: null

        ; !!! `compress:`, `exports:`, and `contents:` were commented out.
        ; exports perhaps because they only applied to modules, and would be
        ; misleading on a plain script.  The other perhaps because they were
        ; not processed by C code so (1) did not apply.  `Needs:` has been
        ; removed while focusing on the IMPORT dialect design.  `Checksum:`
        ; was a basically useless feature, as there's no security benefit to
        ; having the checksum of the body travel in the same file as a fully
        ; tamperable header.
    ]

    scheme: construct [
        name:       ; word of http, ftp, sound, etc.
        title:      ; user-friendly title for the scheme
        spec:       ; custom spec for scheme (if needed)
        info:       ; prototype info object returned from query
;       kind:       ; network, file, driver
;       type:       ; bytes, integers, objects, values, block
        actor:      ; standard action handler for scheme port functions
            null
    ]

    port: construct [ ; Port specification object
        spec: null     ; published specification of the port
        scheme: null   ; scheme object used for this port
        actor: null    ; port action handler (script driven)

        ; !!! Native ports typically used raw C structs stored in a BLOB!
        ; as the `state`.  This makes that state opaque to the garbage
        ; collector, so it is a problem if Value*/Flex* are stored in it.
        ;
        state: null    ; internal state values (private)

        data: null     ; data buffer (usually binary or block)
        locals: null   ; user-defined storage of local data
    ]

    port-spec-head: construct [
        title:      ; user-friendly title for port
        scheme:     ; reference to scheme that defines this port
        ref:        ; reference path or url (for errors)
        path:       ; used for files
           null          ; (extended here)
    ]

    port-spec-net: make port-spec-head [
        host: null
        port-id: 80

        ; Set this to make outgoing packets seem to originate from a specific
        ; port (it's done by calling bind() before the first sendto(),
        ; otherwise the OS will pick an available port and stick with it.)
        ;
        local-id: null

        ; This should be set to a function that takes a PORT! on listening
        ; sockets...it will be called when a new connection is made.
        ;
        accept: null
    ]

    port-spec-signal: make port-spec-head [
        mask: '[all]
    ]

    file-info: construct [
        name:
        size:
        date:
        type:
            null
    ]

    net-info: construct [
        local-ip:
        local-port:
        remote-ip:
        remote-port:
            null
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
    type-spec: construct [
        title: null
    ]

    utype: null
    font: null  ; mezz-graphics.h
    para: null  ; mezz-graphics.h
]


user: construct [
    name:           ; User's name
    home:           ; The HOME environment variable
    words: null
    identity: construct [
        email: smtp: pop3: esmtp-user: esmtp-pass: fqdn: null
    ]
    identities: '[]
]

console: null  ; console (repl) object created by the console extension


cgi: construct [ ; CGI environment variables
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
    content-length: null
    other-headers: '[]
]

; Boot process does sanity check that this eval ends with ~end~ QUASI-WORD!
'~end~
