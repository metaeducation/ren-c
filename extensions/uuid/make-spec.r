REBOL []

name: 'UUID
source: [
    %uuid/mod-uuid.c

    <msc:/wd4459>  ; global shadowing ok, see LIBREBOL_SPECIFIER
]
includes: reduce [
    (join repo-dir %extensions/uuid/libuuid/)
    %prep/extensions/uuid ;for %tmp-extensions-uuid-init.inc
]
depends: switch platform-config/os-base [
    'linux 'Haiku [
        [
            %uuid/libuuid/gen_uuid.c
            %uuid/libuuid/unpack.c
            %uuid/libuuid/pack.c
            %uuid/libuuid/randutils.c
        ]
    ]
] else [null]  ; can't use null fallout in bootstrap

libraries: switch platform-config/os-base [
    'Windows [
        [%rpcrt4]
    ]
] else [null]  ; can't use null fallout in bootstrap

ldflags: switch platform-config/os-base [
    'OSX [
        ["-framework CoreFoundation"]
    ]
] else [null]  ; can't use null fallout in bootstrap

use-librebol: true  ; can't use %sys-core.h with MacOS UUID APIs, conflicts
