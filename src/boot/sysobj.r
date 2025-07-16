Rebol [
    system: "Rebol [R3] Language Interpreter and Run-time Environment"
    title: "System object"
    rights: {
        Copyright 2012 REBOL Technologies
        REBOL is a trademark of REBOL Technologies
    }
    license: {
        Licensed under the Apache License, Version 2.0.
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    purpose: {
        Defines the system object. This is a special block that is evaluted
        such that its words do not get put into the current context.
    }
    notes: "Remove older/unused fields before beta release"
]

; Next five fields are updated during build:
version:  0.0.0
build:    1
platform: null
commit: null

product: null  ; assigned by startup of the host ('core, 'view, 'ren-garden...)

license: {Copyright 2012 REBOL Technologies
REBOL is a trademark of REBOL Technologies
Licensed under the Apache License, Version 2.0.
See: http://www.apache.org/licenses/LICENSE-2.0
}

catalog: construct [
    ;
    ; These catalogs are filled in by Init_System_Object()
    ;
    datatypes: null
    actions: null
    natives: null
    errors: null
]

contexts: construct [
    root:
    sys:
    lib:
    user:
        null
]

state: construct [
    ; Mutable system state variables
    last-error: null ; used by WHY?
]

modules: [] ;loaded modules
extensions: [] ;loaded extensions

codecs: make object! [[][]]

schemes: make object! [[][]]

util: null

ports: construct [
    wait-list: []   ; List of ports to add to 'wait
    pump: []
    input:          ; Port for user input.
    output:         ; Port for user output
    system:         ; Port for system events
    callback: null  ; Port for callback events
]

locale: construct [
    language:   ; Human language locale
    language*: null
    library: null
    ;make object! [modules: utilities: https://raw.githubusercontent.com/r3n/renclib/master/usermodules.reb]
    locale:
    locale*: null
    months: [
        "January" "February" "March" "April" "May" "June"
        "July" "August" "September" "October" "November" "December"
    ]
    days: [
        "Monday" "Tuesday" "Wednesday" "Thursday" "Friday" "Saturday" "Sunday"
    ]
]

set in locale 'library construct [
    modules: https://raw.githubusercontent.com/r3n/renclib/master/usermodules.reb
    utilities: https://raw.githubusercontent.com/r3n/renclib/master/userutils.reb
]

options: construct [  ; Options supplied to REBOL during startup
    bin: null       ; Path to directory where Rebol executable binary lives
    boot: null      ; Path of executable, ie. system/options/bin/r3-exe
    home: null      ; Path of home directory
    resources: null ; users resources directory (for %user.r, skins, modules etc)
    suppress: null  ; block of user --suppress items, eg [%rebol.r %user.r %console-skin.reb]
    loaded: []      ; block with full paths to loaded start-up scripts
    path: null      ; Where script was started or the startup dir

    current-path: null   ; Current URL! or FILE! path to use for relative lookups

    script: null    ; Filename of script to evaluate
    args: null      ; Command line arguments passed to script
    debug: null     ; debug flags
    version: null   ; script version needed

    dump-size: 68   ; used by dump

    quiet: null    ; do not show startup info (compatibility)
    about: null    ; do not show full banner (about) on start-up
    cgi: null
    no-window: null
    verbose: null

    module-paths: [%./]
    default-suffix: %.reb ; Used by IMPORT if no suffix is provided
    file-types: copy [
        %.reb %.r3 %.r rebol
    ]

    ; Legacy Behaviors Options (paid attention to only by debug builds)

    forever-64-bit-ints: null
    unlocked-source: null
]

script: construct [
    title:          ; Title string of script
    header:         ; Script header as evaluated
    parent:         ; Script that loaded the current one
    path:           ; Location of the script being evaluated
    args:           ; args passed to script
        null
]

standard: construct [
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
        return: lambda [
            {Returns a value from an action} value [<end> any-stable!]
        ][
            unwind/with (binding of 'return) either end? 'value [] [:value]
        ]
        #BODY
    ]

    proc-return-type: [trash!]

    proc-body: [
        return: lambda [
            {Returns a value from an action} value [<end> any-stable!]
        ][
            unwind/with (binding of 'return) either end? 'value [] [:value]
        ]
        #BODY
        void
    ]

    ; !!! The PORT! and actor code is deprecated, but this bridges it so
    ; it doesn't have to build a spec by hand.
    ;
    port-actor-spec: [port-actor-parameter [any-stable!]]

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
    action-meta: construct [
        description:
        return-type:
        return-note:
        parameter-types:
        parameter-notes:
            null
    ]

    ; The common case is that derived actions will not need to be
    ; REDESCRIBE'd besides their title.  If they are, then they switch the
    ; meta archetype to `action-meta` and subset the parameters.  Otherwise
    ; HELP just follows the link (`specializee`, `adaptee`) and gets
    ; descriptions there.

    specialized-meta: construct [
        description:
        specializee:
        specializee-name:
            null
    ]

    adapted-meta: construct [
        description:
        adaptee:
        adaptee-name:
            null
    ]

    enclosed-meta: construct [
        description:
        inner:
        inner-name:
        outer:
        outer-name:
            null
    ]

    cascaded-meta: construct [
        description:
        pipeline:
        pipeline-names:
            null
    ]

    ; !!! This is the template used for all errors, to which extra fields are
    ; added if the error has parameters.  It likely makes sense to put this
    ; information into the META-OF of the error, so that parameterizing the
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

    header: construct [
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
            null
    ]

    scheme: construct [
        name:       ; word of http, ftp, sound, etc.
        title:      ; user-friendly title for the scheme
        spec:       ; custom spec for scheme (if needed)
        info:       ; prototype info object returned from query
;       kind:       ; network, file, driver
;       type:       ; bytes, integers, objects, values, block
        actor:      ; standard action handler for scheme port functions
        awake:      ; standard awake handler for this scheme's ports
            null
    ]

    port: construct [ ; Port specification object
        spec:       ; published specification of the port
        scheme:     ; scheme object used for this port
        actor:      ; port action handler (script driven)
        awake:      ; port awake function (event driven)
        state:      ; internal state values (private)
        data:       ; data buffer (usually binary or block)
        locals:     ; user-defined storage of local data

        ; !!! The `connections` field is a BLOCK! used only by TCP listen
        ; ports.  Since it is a Rebol series value, the GC needs to be aware
        ; of it, so it can't be in the port-subtype-specific REBREQ data.
        ; As REBREQ migrates to being Rebol-valued per-port data, this should
        ; be a field only in those TCP listening ports...
        ;
        connections:

;       stats:      ; stats on operation (optional)
            null
    ]

    port-spec-head: construct [
        title:      ; user-friendly title for port
        scheme:     ; reference to scheme that defines this port
        ref:        ; reference path or url (for errors)
        path:       ; used for files
           null            ; (extended here)
    ]

    port-spec-net: make port-spec-head [
        host: null
        port-id: 80

        ; Set this to make outgoing packets seem to originate from a specific
        ; port (it's done by calling bind() before the first sendto(),
        ; otherwise the OS will pick an available port and stick with it.)
        ;
        local-id: null
    ]

    port-spec-signal: make port-spec-head [
        mask: [all]
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

    stats: construct [ ; port stats
        timer:      ; timer (nanos)
        evals:      ; evaluations
        eval-actions:
        series-made:
        series-freed:
        series-expanded:
        series-bytes:
        series-recycled:
        made-blocks:
        made-objects:
        recycles:
            null
    ]

    type-spec: construct [
        title:
        type:
            null
    ]

    utype: null
    font: null  ; mezz-graphics.h
    para: null  ; mezz-graphics.h
]

;;stats: null

;user-license: construct [
;   name:
;   email:
;   id:
;   message:
;       null
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
;           null
;       rts-cts: okay
;       user-data:
;       awake:

;   port-flags: construct [
;       direct:
;       pass-thru:
;       open-append:
;       open-new:
;           null
;   ]

;   email: construct [ ; Email header object
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
;           null
;   ]

user: construct [
   name:           ; User's name
   home:           ; The HOME environment variable
   words: null
   identity: construct [email: smtp: pop3: esmtp-user: esmtp-pass: fqdn: null]
   identities: []
]

;network: construct [
;   host: ""        ; Host name of the user's computer
;   host-address: 0.0.0.0 ; Host computer's TCP-IP address
;   trace: null
;]

console: null  ; console (repl) object created in host-start (os/host-start.r)

; Below is original console construct (unused and comment-out in r3/ren-c)
; Left here for reference (for future development)
;
;console: construct [
;   hide-types: null    ; types not to print
;   history: null       ; Log of user inputs
;   keys: null          ; Keymap for special key
;   prompt:  {>> }   ; Specifies the prompt
;   result:  {== }   ; Specifies result
;   escape:  {(escape)} ; Indicates an escape
;   busy:    {|/-\}  ; Spinner for network progress
;   tab-size: 4      ; default tab size
;   break: okay      ; whether escape breaks or not
;]

;           decimal: #"."   ; The character used as the decimal point in decimal and money vals
;           sig-digits: null    ; Significant digits to use for decimals ; null for normal printing
;           date-sep: #"-"  ; The character used as the date separator
;           date-month-num: null   ; True if months are displayed as numbers; False for names
;           time-sep: #":"  ; The character used as the time separator

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
       other-headers: []
]
;   browser-type: 0

;   trace:          ; True if the --trace flag was specified
;   help: null      ; True if the --help flags was specified
;   halt: null      ; halt after script

;-- Current expectation is that evaluation ends with BLANK!
_
