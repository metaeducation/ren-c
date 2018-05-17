REBOL []

name: 'JPG
source: %jpg/ext-jpg.c
modules: [
    [
        name: 'JPG
        source: %jpg/mod-jpg.c
        depends: [
            ;
            ; The JPG sources come from elsewhere; invasive maintenance for
            ; compiler rigor is not worthwhile to be out of sync with original.
            ;
            [
                %jpg/u-jpg.c

                <gnu:-Wno-unused-parameter> <msc:/wd4100>

                <gnu:-Wno-shift-negative-value>

                ; "conditional expression is constant"
                ;
                <msc:/wd4127>

                ; May 2018 update to MSVC 2017 added warnings about Spectre
                ; mitigation.  The JPG code contains a lot of code that would
                ; trigger slowdown.  It is not a priority to rewrite, given
                ; that some other vetted 3rd party JPG code should be used.
                ;
                <msc:/wd5045> ;-- https://stackoverflow.com/q/50399940
            ]
        ]
    ]
]
