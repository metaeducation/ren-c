Rebol [
    file: %llvm-bc.r
]

compiler: 'clang

with-ffi: 'no

cflags: ["-emit-llvm"]
