REBOL []

name: 'UUID
source: %uuid/mod-uuid.c
includes: reduce [
    (join repo-dir %extensions/uuid/libuuid/)
    %prep/extensions/uuid ;for %tmp-extensions-uuid-init.inc
]
depends: maybe switch system-config/os-base [
    'linux [
        [
            %uuid/libuuid/gen_uuid.c
            %uuid/libuuid/unpack.c
            %uuid/libuuid/pack.c
            %uuid/libuuid/randutils.c
        ]
    ]
]

libraries: maybe switch system-config/os-base [
    'Windows [
        [%rpcrt4]
    ]
]
ldflags: maybe switch system-config/os-base [
    'OSX [
        ["-framework CoreFoundation"]
    ]
]

use-librebol: true  ; can't use %sys-core.h with MacOS UUID APIs, conflicts
