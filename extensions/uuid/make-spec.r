Rebol []

name: 'UUID
source: %uuid/mod-uuid.c
includes: reduce [
    %prep/extensions/uuid  ; for %tmp-extensions-uuid-init.inc
]
depends: switch system-config/os-base [
    'linux [
        [
            %uuid/libuuid/gen_uuid.c
            %uuid/libuuid/unpack.c
            %uuid/libuuid/pack.c
            %uuid/libuuid/randutils.c
        ]
    ]
    'osx [
        [%uuid/uuid-mac.c]
    ]
]

libraries: switch system-config/os-base [
    'windows [
        [%rpcrt4]
    ]
]
ldflags: switch system-config/os-base [
    'osx [
        ["-framework CoreFoundation"]
    ]
]
