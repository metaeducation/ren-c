Rebol []

name: 'Process
source: %process/mod-process.c
includes: copy [
    %prep/extensions/process ;for %tmp-extensions-process-init.inc
]
