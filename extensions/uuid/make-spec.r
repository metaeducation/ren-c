Rebol [
    name: UUID
    notes: "See %extensions/README.md for the format and fields of this file"
]

use-librebol: 'yes  ; can't use %sys-core.h with MacOS UUID APIs, conflicts

includes: [
    %libuuid/
]

; Windows has UUID APIs via library %rpcrt4, OSX has them via CoreFoundation,
; but on Linux/Haiku we need to build in libuuid.
;
depends: switch platform-config.os-base [
    'Linux 'Haiku [
        [
            %libuuid/gen_uuid.c
            %libuuid/unpack.c
            %libuuid/pack.c
            %libuuid/randutils.c
        ]
    ]
]

sources: %mod-uuid.c

libraries: switch platform-config.os-base [
    'Windows [
        %rpcrt4
    ]
]

ldflags: switch platform-config.os-base [
    'OSX [
        ["-framework CoreFoundation"]
    ]
]
