Rebol [
    title: "TCP/UDP Networking"
    name: Network
    type: module
    version: 1.0.0
    license: "Apache 2.0"
]

; The polymorphic WAIT function was once part of the "event" module.  But
; the R3-Alpha event loop was underthought and required cross-platform work
; better done by libuv (in a standard C, cross-platform, documented and
; tested way).  So Ren-C on Linux, Windows, OS X, Haiku etc. are all based
; on libuv I/O for files and networking.
;
; Due to the switch to synchronous I/O, the only things you really WAIT on
; right now are timers and network server ports waiting for connections.
; So for convenience, the WAIT function resides in the network module, to
; avoid needing to go through a level of indirection of a more generic
; "libuv extension" (which the network and filesystem extensions would
; depend on).  Developing that pluggable model is not considered to be as
; high a priority as advancing the WebAssembly build (which does not involve
; libuv at all for its "events").
;
; If using a desktop build without the network extension and you just want
; to wait for a specified amount of time, use SLEEP.
;
; WAIT* expects block to be pre-reduced, to ease stackless implementation
;
export /wait: adapt wait*/ [if block? value [value: reduce value]]

sys.util/make-scheme [
    title: "TCP Networking"
    name: 'tcp
    actor: tcp-actor/
    spec: system.standard.port-spec-net
    info: system.standard.net-info  ; !!! comment here said "for C enums"
]

; NOTE: UDP Networking has not been rewritten for the libuv transition.  It
; is kept here as a reminder of that.
;
sys.util/make-scheme [
    title: "UDP Networking"
    name: 'udp
    actor: udp-actor/
    spec: system.standard.port-spec-net
    info: system.standard.net-info  ; !!! comment here said "for C enums"
]
