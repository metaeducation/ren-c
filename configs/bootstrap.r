REBOL [
    File: %bootstrap.r
]

os-id: 0.4.40

toolset: compose [
    gcc (spaced [system/options/boot {--do "c99" --}])
    ld (spaced [system/options/boot {--do "c99" --}])
]
