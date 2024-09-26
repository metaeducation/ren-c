REBOL []

name: 'UUID
source: [
    %uuid/mod-uuid.c

    <msc:/wd4459>  ; global shadowing ok, see LIBREBOL_BINDING
]
includes: reduce [
    (join repo-dir %extensions/uuid/libuuid/)
    %prep/extensions/uuid ;for %tmp-extensions-uuid-init.inc
]
depends: switch platform-config.os-base [
    'linux 'Haiku [
        [
            %uuid/libuuid/gen_uuid.c
            %uuid/libuuid/unpack.c
            %uuid/libuuid/pack.c
            %uuid/libuuid/randutils.c
        ]
    ]
]

libraries: switch platform-config.os-base [
    'Windows [
        [%rpcrt4]
    ]
]

ldflags: switch platform-config.os-base [
    'OSX [
        ["-framework CoreFoundation"]
    ]
]

use-librebol: 'yes  ; can't use %sys-core.h with MacOS UUID APIs, conflicts
