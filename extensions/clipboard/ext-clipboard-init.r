Rebol [
    title: "Clipboard Extension"
    name: Clipboard
    type: module
    version: 1.0.0
    license: "Apache 2.0"
]

; The clipboard is registered as a PORT! under the clipboard:// scheme.
;
; Its handler is a "native actor" in C that handles its methods via a
; `switch()` statement on SYM_XXX constants, as opposed to a Rebol OBJECT!
; with FUNCTION!s in it dispatched via words.
;
;
sys.util/make-scheme [
    title: "Clipboard"
    name: 'clipboard
    actor: clipboard-actor/
]
