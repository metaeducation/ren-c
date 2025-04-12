REBOL [
    Name: JavaScript
    Notes: "See %extensions/README.md for the format and fields of this file"
]

sources: [
    %mod-javascript.c

    ; Emscripten says "wontfix"
    ; https://github.com/emscripten-core/emscripten/issues/7113
    ;
    <clang:-Wno-dollar-in-identifier-extension>
]
