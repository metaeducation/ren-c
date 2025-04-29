Rebol [
    file: %bootstrap.r
]

os-id: 0.4.40

compiler: 'tcc
compiler-path: spaced [system.options.boot -{--do "c99" --}-]
