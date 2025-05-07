Rebol [
    title: "Environment Extension"
    name: Environment
    type: module
    version: 1.0.0
    license: "LGPL 3.0"
]

; !!! Review: idea was that these would be put in the SYSTEM context, so you
; would say SYS.ENV - idea is you don't want to go contaminating global space
; with every extension.  But how to do such an insertion into system as a
; module and be consistent with isolation?

export environment: make-environment
export env: environment


; COMPATIBILITY FUNCTIONS
;
; Historical Rebol didn't have space for an ENVIRONMENT! datatype.  Ren-C
; does, but create some compatibility functions while that's tested out.

export get-env: func [
    "Returns the value of an OS environment variable (for current process)"

    return: "String the variable was set to, or null if not set"
        [~null~ text!]
    variable "Name of variable to get (case-insensitive in Windows)"
        [<opt-out> text! word!]
][
    return try environment.(variable)  ; GET-ENV returned null if not there
]

export set-env: func [
    "Sets value of operating system environment variable for current process"

    return: "Returns same value passed in, or null if variable was unset"
        [~null~ text!]
    variable "Variable to set (case-insensitive in Windows)"
        [<opt-out> text! word!]
    value "Value to set the variable to, or null to unset it"
        [~null~ text!]
][
    return environment.(variable): value
]
