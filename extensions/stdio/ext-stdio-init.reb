REBOL [
    Title: "Standard Input/Output"
    Name: StdIO
    Type: Module
    Version: 1.0.0
    License: {Apache 2.0}
]

register-stdio-device

sys/make-scheme [
    title: "Console Access"
    name: 'console
    actor: get-console-actor-handle
]

system/ports/input: open [scheme: 'console]


; During boot, there shouldn't be any output.  However, it would be annoying
; to have to write different versions of every PRINT-based debug routine out
; there which might be used after boot, just because it might be used before.
;
; We use HIJACK because if we just overwrote LIB/WRITE-STDOUT with the new
; function, it would not affect existing specializations and usages.

hijack :lib/write-stdout :write-stdout


; This is the tab-complete command.  It may be that managing the state as a
; BLOCK! containing textual parts and a cursor would be cleaner, e.g.
;
;    ["before" | "after"]
;
; But for now it's the buffer to edit and the position the cursor was in.
;
tab-complete: func [
    return: "new cursor position in the buffer"
        [integer!]
    buffer "buffer to edit into the new state (modified)"
        [text!]
    pos "where the cursor was prior to the edit (0-based)"
        [integer!]
][
    clear buffer
    insert buffer "For example"
    return 3
]

sys/export [tab-complete]
