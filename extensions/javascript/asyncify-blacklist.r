;
; Asyncify does a fair bit of auto-detection of what routines don't need
; to be instrumented.  But you can provide your own whitelist/blacklist.
;
; https://emscripten.org/docs/porting/asyncify.html#optimizing
;
; The file format it accepts is in JSON format, but JSON does not allow
; comments:
;
; https://stackoverflow.com/questions/244777/can-comments-be-used-in-json
;
; Hence this list of strings (which can be commented) is transformed into
; JSON with the comments removed as part of the `make prep` step.
;


; Startup calls the evaluator (that needs to yield in a general sense), but it
; does so before the JavaScript extension has had a chance to load.  And that
; extension contains the natives that call emscripten_sleep().
;
; On the downside, this means it can't be debugged except with `printf()`
; during boot.  On the upside, it means that there is no way it can need to
; yield...so it can run at full speed in WASM with no asyncify instrumentation.
;
; By a similar token, Shutdown happens after the JS extension is unloaded
; and also cannot experience yields.
;
"Startup_Core"
"Shutdown_Core"

; It seems that failing is believed to potentially cause a yield, this should
; be looked into.
;
"Fail_Core"
