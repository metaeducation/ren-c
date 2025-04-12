REBOL [
    File: %llvm-bc.r
]

compiler: 'clang

extensions: [
    - ODBC _
]

with-ffi: 'no

cflags: ["-emit-llvm"]
