REBOL [
    Title: "Clipboard Extension"
    name: 'Clipboard
    type: 'Extension
    version: 1.0.0
    license: {Apache 2.0}
]

; The clipboard is registered as a PORT! under the clipboard:// scheme.
;
; Its handler is a "native actor" in C that handles its methods via a
; `switch()` statement on SYM_XXX constants, as opposed to a Rebol OBJECT!
; with FUNCTION!s in it dispatched via words.
;
;
sys/make-scheme [
    title: "Clipboard"
    name: 'clipboard
    actor: get-clipboard-actor-handle
]
