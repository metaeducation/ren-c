REBOL []

name: 'UUID
source: %uuid/mod-uuid.c
includes: reduce [
    repo-dir/extensions/uuid/libuuid
    %prep/extensions/uuid ;for %tmp-extensions-uuid-init.inc
]
depends: switch system-config/os-base [
    'Linux [
        [
            %uuid/libuuid/gen_uuid.c
            %uuid/libuuid/unpack.c
            %uuid/libuuid/pack.c
            %uuid/libuuid/randutils.c
        ]
    ]
    'OSX [
        repo-dir/extensions/uuid/uuid-mac.c
    ]
]

libraries: switch system-config/os-base [
    'Windows [
        [%rpcrt4]
    ]
]
ldflags: switch system-config/os-base [
    'OSX [
        ["-framework CoreFoundation"]
    ]
]
