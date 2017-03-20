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

; Next four fields are updated during build:
version:  0.0.0
build:    1
platform: _

product: _ ;-- assigned by startup of the host ('core, 'view, 'ren-garden...)

license: {Copyright 2012 REBOL Technologies
REBOL is a trademark of REBOL Technologies
Licensed under the Apache License, Version 2.0.
See: http://www.apache.org/licenses/LICENSE-2.0
}

; !!! HAS is defined later, so this uses CONSTRUCT [] [body] instead.
; MAKE OBJECT! is not used because that is too low-level (no evaluation or
; collection of fields).  Reconsider if base-funcs should be loaded before
; the system object here, or if it should be able to work with just the
; low level MAKE OBJECT! and not use things like `x: y: z: none` etc.

catalog: construct [] [
    ;
    ; These catalogs are filled in by Init_System_Object()
    ;
    datatypes: _
    actions: _
    natives: _
    errors: _
]

contexts: construct [] [
    root:
    sys:
    lib:
    user:
        _
]

state: construct [] [
    ; Mutable system state variables
    note: "contains protected hidden fields"
    policies: construct [] [ ; Security policies
        file:    ; file access
        net:     ; network access
        eval:    ; evaluation limit
        memory:  ; memory limit
        secure:  ; secure changes
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

modules: [] ;loaded modules
extensions: [] ;loaded extensions

codecs: make object! [[][]]

schemes: make object! [[][]]

ports: construct [] [
    wait-list: []   ; List of ports to add to 'wait
    input:          ; Port for user input.
    output:         ; Port for user output
    echo:           ; Port for echoing output
    system:         ; Port for system events
    callback: _     ; Port for callback events
;   serial: _       ; serial device name block
]

locale: construct [] [
    language:   ; Human language locale
    language*:
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

options: construct [] [  ; Options supplied to REBOL during startup
    boot: _         ; The path to the executable
    home: _         ; Path of home directory
    path: _         ; Where script was started or the startup dir

    current-path: _ ; Current URL! or FILE! path to use for relative lookups

    script: _       ; Filename of script to evaluate
    args: _         ; Command line arguments passed to script
    debug: _        ; debug flags
    secure: _       ; security policy
    version: _      ; script version needed

    quiet: false    ; do not show startup info (compatibility)
    cgi: false
    no-window: false
    verbose: false

    binary-base: 16    ; Default base for FORMed binary values (64, 16, 2)
    decimal-digits: 15 ; Max number of decimal digits to print.
    module-paths: [%./]
    default-suffix: %.reb ; Used by IMPORT if no suffix is provided
    file-types: []
    result-types: _

    ; Legacy Behaviors Options (paid attention to only by debug builds)

    lit-word-decay: false
    exit-functions-only: false
    broken-case-semantics: false
    refinements-blank: false
    forever-64-bit-ints: false
    print-forms-everything: false
    break-with-overrides: false
    none-instead-of-voids: false
    dont-exit-natives: false
    paren-instead-of-group: false
    get-will-get-anything: false
    no-reduce-nested-print: false
    arg1-arg2-arg3-error: false
    unlocked-source: false

    ; These option will only apply if the function which is currently executing
    ; was created after legacy mode was enabled, and if refinements-blank is
    ; set (because that's what marks functions as "legacy" or not")
    ;
    no-switch-evals: false
    no-switch-fallthrough: false
]

script: construct [] [
    title:          ; Title string of script
    header:         ; Script header as evaluated
    parent:         ; Script that loaded the current one
    path:           ; Location of the script being evaluated
    args:           ; args passed to script
        _
]

standard: construct [] [
    ; FUNC+PROC implement a native-optimized variant of a function generator.
    ; This is the body template that it provides as the code *equivalent* of
    ; what it is doing (via a more specialized/internal method).  Though
    ; the only "real" body stored and used is the one the user provided
    ; (substituted in #BODY), this template is used to "lie" when asked what
    ; the BODY-OF the function is.
    ;
    ; The substitution location is hardcoded at index 5.  It does not "scan"
    ; to find #BODY, just asserts the position is an ISSUE!.
    ;
    func-body: [
        return: make function! [
            [{Returns a value from a function.} value [<opt> any-value!]]
            [exit/from/with (context-of 'return) :value]
        ]
        leave: make function! [
            [{Leaves a function, giving no result to the caller.}]
            [exit/from (context-of 'leave)]
        ]
        #BODY
    ]

    func-no-leave-body: [
        return: make function! [
            [{Returns a value from a function.} value [<opt> any-value!]]
            [exit/from/with (context-of 'return) :value]
        ]
        #BODY
    ]

    proc-body: [
        leave: make function! [
            [{Leaves a procedure, giving no result to the caller.}]
            [exit/from (context-of 'leave)]
        ]
        #BODY
        comment {No return value.}
    ]

    ; !!! The PORT! and actor code is deprecated, but this bridges it so
    ; it doesn't have to build a spec by hand.
    ;
    port-actor-spec: [port-actor-parameter [<opt> any-value!]]

    ; !!! The %sysobj.r initialization currently runs natives (notably the
    ; natives for making objects, and here using COMMENT because it can).
    ; This means that if the FUNCTION-META information is going to be produced
    ; from a spec block for natives, it wouldn't be available while the
    ; natives are getting initialized.
    ;
    ; It may be desirable to sort out this dependency by using a construction
    ; syntax and making this a MAP! or OBJECT! literal.  In the meantime,
    ; the archetypal context has to be created "by hand" for natives to use,
    ; with this archetype used by the REDESCRIBE Mezzanine.
    ;
    function-meta: construct [] [
        description:
        return-type:
        return-note:
        parameter-types:
        parameter-notes:
            _
    ]

    ; The common case is that derived functions will not need to be
    ; REDESCRIBE'd besides their title.  If they are, then they switch the
    ; meta archetype to `function-meta` and subset the parameters.  Otherwise
    ; HELP just follows the link (`specializee`, `adaptee`) and gets
    ; descriptions there.
    ;
    ; !!! Due to wanting R3-Alpha to be able to run the bootstrap build,
    ; these objects can't unset these fields.  (make object! [x: ()] fails)
    ; Hence the code has to overwrite the missing fields with voids.

    specialized-meta: construct [] [
        description:
        specializee:
        specializee-name:
            _
    ]

    adapted-meta: construct [] [
        description:
        adaptee:
        adaptee-name:
            _
    ]

    chained-meta: construct [] [
        description:
        chainees:
        chainee-names:
            _
    ]

    hijacked-meta: construct [] [
        description:
        hijackee:
        hijackee-name:
            _
    ]

    error: construct [] [ ; Template used for all errors:
        code: _
        type: 'user
        id: 'message
        message: _
        near: _
        where: _

        ; Arguments will be allocated in the context at creation time if
        ; necessary (errors with no arguments will just have a message)
    ]

    script: construct [] [
        title:
        header:
        parent:
        path:
        args:
            _
    ]

    header: construct [] [
        title: {Untitled}
        name:
        type:
        version:
        date:
        file:
        author:
        needs:
        options:
        checksum:
;       compress:
;       exports:
;       content:
            _
    ]

    scheme: construct [] [
        name:       ; word of http, ftp, sound, etc.
        title:      ; user-friendly title for the scheme
        spec:       ; custom spec for scheme (if needed)
        info:       ; prototype info object returned from query
;       kind:       ; network, file, driver
;       type:       ; bytes, integers, objects, values, block
        actor:      ; standard action handler for scheme port functions
        awake:      ; standard awake handler for this scheme's ports
            _
    ]

    port: construct [] [ ; Port specification object
        spec:       ; published specification of the port
        scheme:     ; scheme object used for this port
        actor:      ; port action handler (script driven)
        awake:      ; port awake function (event driven)
        state:      ; internal state values (private)
        data:       ; data buffer (usually binary or block)
        locals:     ; user-defined storage of local data
;       stats:      ; stats on operation (optional)
            _
    ]

    port-spec-head: construct [] [
        title:      ; user-friendly title for port
        scheme:     ; reference to scheme that defines this port
        ref:        ; reference path or url (for errors)
        path:       ; used for files
           _            ; (extended here)
    ]

    port-spec-net: construct port-spec-head [
        host: _
        port-id: 80
    ]

    port-spec-serial: construct port-spec-head [
        speed: 115200
        data-size: 8
        parity: _
        stop-bits: 1
        flow-control: _ ;not supported on all systems
    ]

    port-spec-signal: construct port-spec-head [
        mask: [all]
    ]

    file-info: construct [] [
        name:
        size:
        date:
        type:
            _
    ]

    net-info: construct [] [
        local-ip:
        local-port:
        remote-ip:
        remote-port:
            _
    ]

    extension: construct [] [
        header:     ; extension hader: name, version, etc.
        script:     ; script to run after the extension is loaded. Could be
                    ; string! (uncompressed code) or binary! (compressed code).
                    ; Could be folded into "header", because it's only used to
                    ; pass script from the extension to LOAD-EXTENSION, and
                    ; cleared once the extensino is loaded. But keeping it
                    ; separate makes its purpose clear...
        lib-base:   ; handle to DLL
        lib-file:   ; file name loaded
        modules:
            _
    ]

    stats: construct [] [ ; port stats
        timer:      ; timer (nanos)
        evals:      ; evaluations
        eval-natives:
        eval-functions:
        series-made:
        series-freed:
        series-expanded:
        series-bytes:
        series-recycled:
        made-blocks:
        made-objects:
        recycles:
            _
    ]

    type-spec: construct [] [
        title:
        type:
            _
    ]

    utype: _
    font: _  ; mezz-graphics.h
    para: _  ; mezz-graphics.h
]

view: construct [] [
    screen-gob: _
    handler: _
    event-port: _
    event-types: [
        ; Event types. Order dependent for C and REBOL.
        ; Due to fixed C constants, this list cannot be reordered after release!
        ignore          ; ignore event (0)
        interrupt       ; user interrupt
        device          ; misc device request
        callback        ; callback event
        custom          ; custom events
        error
        init

        open
        close
        connect
        accept
        read
        write
        wrote
        lookup

        ready
        done
        time

        show
        hide
        offset
        resize
        rotate
        active
        inactive
        minimize
        maximize
        restore

        move
        down
        up
        alt-down
        alt-up
        aux-down
        aux-up
        key
        key-up ; Move above when version changes!!!

        scroll-line
        scroll-page

        drop-file
    ]
    event-keys: [
        ; Event types. Order dependent for C and REBOL.
        ; Due to fixed C constants, this list cannot be reordered after release!
        page-up
        page-down
        end
        home
        left
        up
        right
        down
        insert
        delete
        f1
        f2
        f3
        f4
        f5
        f6
        f7
        f8
        f9
        f10
        f11
        f12
    ]
]

;;stats: _

;user-license: context [
;   name:
;   email:
;   id:
;   message:
;       _
;]



; (returns value)

;       model:      ; Network, File, Driver
;       type:       ; bytes, integers, values
;       user:       ; User data

;       host:
;       port-id:
;       user:
;       pass:
;       target:
;       path:
;       proxy:
;       access:
;       allow:
;       buffer-size:
;       limit:
;       handler:
;       status:
;       size:
;       date:
;       sub-port:
;       locals:
;       state:
;       timeout:
;       local-ip:
;       local-service:
;       remote-service:
;       last-remote-service:
;       direction:
;       key:
;       strength:
;       algorithm:
;       block-chaining:
;       init-vector:
;       padding:
;       async-modes:
;       remote-ip:
;       local-port:
;       remote-port:
;       backlog:
;       device:
;       speed:
;       data-bits:
;       parity:
;       stop-bits:
;           _
;       rts-cts: true
;       user-data:
;       awake:

;   port-flags: construct [] [
;       direct:
;       pass-thru:
;       open-append:
;       open-new:
;           _
;   ]

;   email: construct [] [ ; Email header object
;       To:
;       CC:
;       BCC:
;       From:
;       Reply-To:
;       Date:
;       Subject:
;       Return-Path:
;       Organization:
;       Message-Id:
;       Comment:
;       X-REBOL:
;       MIME-Version:
;       Content-Type:
;       Content:
;           _
;   ]

;user: construct [] [
;   name:           ; User's name
;   email:          ; User's default email address
;   home:           ; The HOME environment variable
;   words: _
;]

;network: construct [] [
;   host: ""        ; Host name of the user's computer
;   host-address: 0.0.0.0 ; Host computer's TCP-IP address
;   trace: _
;]

;console: construct [] [
;   hide-types: _    ; types not to print
;   history: _       ; Log of user inputs
;   keys: _          ; Keymap for special key
;   prompt:  {>> }   ; Specifies the prompt
;   result:  {== }   ; Specifies result
;   escape:  {(escape)} ; Indicates an escape
;   busy:    {|/-\}  ; Spinner for network progress
;   tab-size: 4      ; default tab size
;   break: true      ; whether escape breaks or not
;]

;           decimal: #"."   ; The character used as the decimal point in decimal and money vals
;           sig-digits: _    ; Significant digits to use for decimals ; blank for normal printing
;           date-sep: #"-"  ; The character used as the date separator
;           date-month-num: false   ; True if months are displayed as numbers; False for names
;           time-sep: #":"  ; The character used as the time separator
;   cgi: construct [] [ ; CGI environment variables
;       server-software:
;       server-name:
;       gateway-interface:
;       server-protocol:
;       server-port:
;       request-method:
;       path-info:
;       path-translated:
;       script-name:
;       query-string:
;       remote-host:
;       remote-addr:
;       auth-type:
;       remote-user:
;       remote-ident:
;       Content-Type:           ; cap'd for email header
;       content-length: _
;       other-headers: []
;   ]
;   browser-type: 0

;   trace:          ; True if the --trace flag was specified
;   help: _         ; True if the --help flags was specified
;   halt: _         ; halt after script

;-- Expectation is that evaluation ends with no result, empty GROUP! does that
()
