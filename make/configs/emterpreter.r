REBOL []

os-id: 0.16.1 ; JS, web, emterpreter

; Right now, either #web or #node
;
javascript-environment: #web


; The inability to communicate synchronously between the worker and GUI in
; JavaScript means that being deep in a C-based interpreter stack on the
; worker cannot receive data from the GUI.  The "Emterpreter" works around
; this limitation by running a JavaScript bytecode simulator...so even if
; a JavaScript stack can't be paused, the bytecode interpreter can, long
; enough to release the GUI thread it was running on to do let it do work.
;
; https://github.com/emscripten-core/emscripten/wiki/Emterpreter
;
; That's a slow and stopgap measure, which makes the build products twice as
; large and much more than twice as slow.  It is supplanted entirely with a
; superior approach based on WASM threads.  In this model, the GUI thread is
; left free, while the code that's going to make demands runs on its own
; thread...which can suspend and wait, using conventional atomics (mutexes,
; wait conditions).
;
; There was some issue with WASM threading being disabled in 2018 due to
; Spectre vulnerabilities in SharedArrayBuffer.  This seems to be mitigated,
; and approaches are now focusing on assuming that the thread-based solution
; will be available.  The emterpreter method is preserved as a fallback, but
; should not be used without good reason--only basic features are implemented.
;
use-emterpreter: true

use-wasm: false

config: %emscripten.r
