REBOL []

name: 'Crypt
loadable: no ;tls depends on this, so it has to be builtin
source: %crypt/ext-crypt.c
init: %crypt/ext-crypt-init.reb
modules: [
    [
        name: 'Crypt
        source: %crypt/mod-crypt.c
        includes: reduce [
            ;
            ; Added so `#include "bigint/bigint.h` can be found by %rsa.h
            ; and `#include "rsa/rsa.h" can be found by %dh.c
            ;
            src-dir/extensions/crypt
            %prep/extensions/crypt ;for %tmp-extensions-crypt-init.inc
        ]
        depends: [
            [
                %crypt/aes/aes.c

                ; May 2018 update to MSVC 2017 added warnings about Spectre
                ; mitigation.  The JPG code contains a lot of code that would
                ; trigger slowdown.  It is not a priority to rewrite, given
                ; that some other vetted 3rd party JPG code should be used.
                ;
                <msc:/wd5045> ;-- https://stackoverflow.com/q/50399940
            ]

            [
                %crypt/bigint/bigint.c

                ; See above remarks on Spectre.  This may be a priority to
                ; address, if bigint is used in INTEGER!.
                ;
                <msc:/wd5045> ;-- https://stackoverflow.com/q/50399940
            ]

            %crypt/dh/dh.c
            %crypt/rc4/rc4.c
            %crypt/rsa/rsa.c
            %crypt/sha256/sha256.c
        ]
    ]
]
