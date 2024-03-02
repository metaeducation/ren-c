REBOL []

name: 'UUID
source: %uuid/mod-uuid.c
includes: reduce [
    repo-dir/extensions/uuid/libuuid
    %prep/extensions/uuid ;for %tmp-extensions-uuid-init.inc
]
depends: null-to-blank switch system-config/os-base [
    'linux [
        [
            %uuid/libuuid/gen_uuid.c
            %uuid/libuuid/unpack.c
            %uuid/libuuid/pack.c
            %uuid/libuuid/randutils.c
        ]
    ]
]

libraries: null-to-blank switch system-config/os-base [
    'Windows [
        [%rpcrt4]
    ]
]
ldflags: null-to-blank switch system-config/os-base [
    'OSX [
        ["-framework CoreFoundation"]
    ]
]
