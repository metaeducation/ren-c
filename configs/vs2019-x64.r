REBOL [
    File: %vs2019-x64.r
]

os-id: 0.3.40

target: 'nmake

toolset: [
    cl %cl.exe
    link %link.exe
]

with-ffi: [
    definitions: ["FFI_BUILDING"]  ; the prebuilt library is static

    includes: [%../external/ffi-prebuilt/msvc/lib64/libffi-3.2.1/include]

    ; Change to .../Debug for debugging build
    ;
    searches: [%../external/ffi-prebuilt/msvc/lib64/Release]

    libraries: reduce [make rebmake/ext-static-class [output: %libffi.lib]]
]

rebol-tool: %r3-make.exe
