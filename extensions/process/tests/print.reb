Rebol []

; !!! system.script.args can come from DO/ARGS or the command line
; system.options.args is only what the original command line said,
; it will be empty if run with DO/ARGS

count-up n to-integer first system.script.args [
    prin "."
]
