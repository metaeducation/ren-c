REBOL []
toolset: [
    clang
    llvm-link
]

extensions: [
    - ODBC _
]
cflags: ["-emit-llvm"]
