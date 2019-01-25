REBOL []

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
            %zmq
            ;"C:\Program Files\ZeroMQ 4.0.4\lib\libzmq-v120-mt-gd-4_0_4.lib"
        ]
    ]
    default [
        [%zmq]
    ]
]

options: [
    ;e.g. odbc-requires-ltdl [word! logic! blank!] ()
]
