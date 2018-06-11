REBOL []

name: 'ZeroMQ
source: %zeromq/ext-zeromq.c
init: %zeromq/ext-zeromq-init.reb
modules: [
    [
        name: 'ZeroMQ
        source: [
            %zeromq/mod-zeromq.c

            ; !!! Put things like <msc:/wdXXXX> here if needed
        ]
        includes: [
            %prep/extensions/zeromq ;for %tmp-ext-zeromq-init.inc
            ;"C:\Program Files\ZeroMQ 4.0.4\include"
        ]
        libraries: switch system-config/os-base [
            'Windows [
                [
                    ;%odbc32
                    ;"C:\Program Files\ZeroMQ 4.0.4\lib\libzmq-v120-mt-gd-4_0_4.lib"
                ]
            ]
            default [
                [%zmq]
            ]
            ; On some systems (32-bit Ubuntu 12.04), odbc requires ltdl
            ;append-of [%odbc] if not find [no false off _ #[false]] user-config/odbc-requires-ltdl [%ltdl]
        ]
    ]
]

options: [
    ;e.g. odbc-requires-ltdl [word! logic! blank!] ()
]
