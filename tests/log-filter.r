Rebol [
    title: "Log filter"
    file: %log-filter.r
    copyright: [2012 "Saphirion AG"]
    license: --[
        Licensed under the Apache License, Version 2.0 (the "License");
        you may not use this file except in compliance with the License.
        You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
    ]--
    author: "Ladislav Mecir"
    purpose: "Test framework"
]

import %test-parsing.r

log-filter: func [
    return: []
    source-log [file!]
][
    ; if the source log is r_2_7_8_3_1_1DEF65_002052.log
    ; the target log will be f_2_7_8_3_1_1DEF65_002052.log
    ; , i.e., using the "f" prefix
    let target-log: copy source-log
    change target-log %f

    if exists? target-log [delete target-log]

    collect-logs source-log-contents: copy [] source-log

    for-each [source-test source-result] source-log-contents [
        if find [crashed failed] source-result [
            ; test failure
            write:append target-log spaced [
                source-test _ mold source-result _ newline
            ]
        ]
    ]
]

log-filter to-file system.script.args
