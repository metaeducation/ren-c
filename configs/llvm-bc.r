REBOL [
    File: %llvm-bc.r
]

toolset: [
    clang
    llvm-link
]

extensions: [
    - ODBC _
]

with-ffi: 'no

cflags: ["-emit-llvm"]
