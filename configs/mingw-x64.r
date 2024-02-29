REBOL []

os-id: 0.3.40
toolset: [
    gcc %x86_64-w64-mingw32-gcc
    ld %x86_64-w64-mingw32-gcc; linking is done via calling gcc, not ld
    strip %x86_64-w64-mingw32-strip
]
