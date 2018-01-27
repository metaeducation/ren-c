REBOL []

os-id: 0.16.0

gcc-path: 

toolset: [
    gcc %emcc
    ld %emcc
]

optimize: "z"

ldflags: reduce [unspaced["-O" optimize]]

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
]
