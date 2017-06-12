Rebol [
    ;needs: [changes-file]
]

import 'changes-file
changes-block: make-changes-block get-git-log
make-changes-file %../../CHANGES.md %../../CREDITS.md changes-block

