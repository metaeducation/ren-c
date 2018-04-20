REBOL []

os-id: 0.16.2

gcc-path: 

toolset: [
    gcc %emcc
    ld %emcc
]

optimize: "z"

ldflags: reduce [
    unspaced ["-O" optimize]
    unspaced [{-s 'ASSERTIONS=} either debug [1] [0] {'}]
    {-s 'EXTRA_EXPORTED_RUNTIME_METHODS=["ccall", "cwrap"]'}
    {--post-js prep/include/reb-lib.js}
]

extensions: [
    - crypt _
    - process _
    - view _
    - png _
    - gif _
    - jpg _
    - bmp _
    - locale _
    - uuid _
    - odbc _
    - ffi _
    - debugger _
    - Clipboard _
]
