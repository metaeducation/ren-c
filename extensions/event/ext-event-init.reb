REBOL [
    Title: "EVENT! Extension"
    Name: Event
    Type: Module
    Options: [isolate]
    Version: 1.0.0
    License: {Apache 2.0}

    Description: {
        This registers the EVENT! data type and makes the "Event Port".  This
        was previously a part of the interpreter core, and so moving it to an
        extension is a "big deal" as it is no longer intrinsic to the
        evaluator or the language--you can do Rebol builds without this.

        (The fact it was called "System Port" hints at how it was thought of
        as essential to the system... Ren-C doesn't see it that way, as using
        a service like PARSE embedded in another language may have no need
        for such things).

        Although it uses generic services, every attempt has been made to
        stick to the principles of the original event system--including that
        EVENT! can fit in one cell.
    }

    Notes: {
        See %extensions/event/README.md
    }
]

; !!! Should call UNREGISTER-EVENT-HOOKS at some point (module finalizer?)
;
register-event-hooks


sys/make-scheme [
    title: "System Port"
    name: 'system
    actor: get-event-actor-handle
    awake: func [
        sport "System port (State block holds events)"
        ports "Port list (Copy of block passed to WAIT)"
        /only
        <local> event event-list n-event port waked
    ][
        waked: sport/data ; The wake list (pending awakes)

        if only and [not block? ports] [
            return blank ; short cut for a pause
        ]

        ; Process all events (even if no awake ports)

        n-event: 0
        event-list: sport/state
        while [not empty? event-list] [
            ;
            ; Do only 8 events at a time (to prevent polling lockout)
            ;
            if n-event > 8 [break]

            event: first event-list
            port: event/port

            if (not only) or [find ports port] [
                remove event-list  ; avoid overflow from WAKE-UP calling WAIT

                if wake-up port event [
                    ;
                    ; Add port to wake list:
                    ;
                    ** -- /system-waked port/spec/ref
                    if not find waked port [append waked @port]
                ]
                n-event: n-event + 1
            ]
            else [
                event-list: next event-list
            ]
        ]

        if not block? ports [return blank]  ; no wake ports (just a timer)

        ; Are any of the requested ports awake?
        ;
        for-each port ports [
            find waked port then [return true]
        ]

        false  ; keep waiting
    ]
    init: func [port] [
        ** print ["Init" title]
        port/data: copy []  ; The port wake list
        return
    ]
]

system/ports/system: open [scheme: 'system]

sys/export []  ; current hacky mechanism is to put any exports here
