REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "REBOL 3 Boot Base: Function Constructors"
    Rights: {
        Copyright 2012 REBOL Technologies
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Note: {
        This code is evaluated just after actions, natives, sysobj, and other lower
        levels definitions. This file intializes a minimal working environment
        that is used for the rest of the boot.
    }
]

does: func [
    {A shortcut to define a function that has no arguments or locals.}
    body [block!] {The body block of the function}
][
    func [] body
]

; We do use the RETURN native spec (for now) to provide the prototype for
; the fake definitional return.  But the only way you should be able to get
; at *that* return is through the FUNC and CLOS generators (when they hack
; out its function pointer to do the implement the EXT_FUNC_HAS_RETURN)
;
return: func [] [
    fail "RETURN called--but no function generator providing it in use"
]

function: func [
    ; !!! Should have a unified constructor with CLOSURE
    {Defines a function with all set-words as locals.}
    spec [block!] {Help string (opt) followed by arg words (and opt type and string)}
    body [block!] {The body block of the function}
    /with {Define or use a persistent object (self)}
    object [object! block! map!] {The object or spec}
    /extern words [block!] {These words are not local}
][
    ; Copy the spec and add /local to the end if not found (no deep copy needed)
    unless find spec: copy spec /local [append spec [
        /local ; In a block so the generated source gets the newlines
    ]]

    ; Collect all set-words in the body as words to be used as locals, and add
    ; them to the spec. Don't include the words already in the spec or object.
    insert find/tail spec /local collect-words/deep/set/ignore body either with [
        ; Make our own local object if a premade one is not provided
        unless object? object [object: make object! object]

        ; Make a full copy of the body, to allow reuse of the original
        body: copy/deep body

        bind body object  ; Bind any object words found in the body

        ; Ignore the words in the spec and those in the object. The spec needs
        ; to be copied since the object words shouldn't be added to the locals.
        ; ignore 'self too
        compose [(spec) 'self (words-of object) (:words)]
    ][
        ; Don't include the words in the spec, or any extern words.
        either extern [append copy spec words] [spec]
    ]

    func spec body
]

use: func [
    {Defines words local to a block.}
    vars [block! word!] {Local word(s) to the block}
    body [block!] {Block to evaluate}
][
    ; We are building a CLOS out of the body that was passed to us, and that
    ; body may have RETURN words with bindings in them already that we do
    ; not want to disturb with the definitional bindings in the new code.
    ; So that means either using MAKE CLOSURE! (which wouldn't disrupt
    ; RETURN bindings) or using the more friendly CLOS with <transparent>
    ; (they do the same thing, just CLOS is arity-2)
    ;
    eval clos compose [<transparent> /local (vars)] body
]

object: func [
    {Defines a unique object.}
    blk [block!] {Object words and values (modified)}
][
    make object! append blk none
]

module: func [
    "Creates a new module."
    spec [block!] "The header block of the module (modified)"
    body [block!] "The body block of the module (modified)"
    /mixin "Mix in words from other modules"
    words [object!] "Words collected into an object"
][
    make module! unbind/deep reduce pick [[spec body] [spec body words]] not mixin
]

cause-error: func [
    "Causes an immediate error throw with the provided information."
    err-type [word!]
    err-id [word!]
    args
][
    ; Make sure it's a block:
    args: compose [(:args)]
    ; Filter out functional values:
    forall args [
        if any-function? first args [
            change/only args spec-of first args
        ]
    ]
    ; Build and throw the error:
    fail make error! [
        type: err-type
        id:   err-id
        arg1: first args
        arg2: second args
        arg3: third args
    ]
]

default: func [
    "Set a word to a default value if it hasn't been set yet."
    'word [word! set-word! lit-word!] "The word (use :var for word! values)"
    value "The value" ; unset! not allowed on purpose
][
    unless all [value? word not none? get word] [set word :value] :value
]

secure: func ['d] [boot-print "SECURE is disabled"]

