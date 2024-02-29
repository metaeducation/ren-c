REBOL []

; will be applied on top of default-config.r

debug: default ['symbols]
toolset: [
    gcc %g++
    ld %g++
]
standard: default ['c++]
