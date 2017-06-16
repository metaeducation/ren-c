Rebol [
    title:   {Module for making CHANGES.md file listing notable changes by version and category}
    type:    module
    name:    changes-file
    file:    %changes-file.reb
    author:  "Barry Walsh (draegtun)"
    date:    16-Jun-2017
    ;; needs:   [2.102.0] ;; Ren/C
    version: 0.1.6
    history: [
        0.1.0  30-May-2017  {Protoype. Created initial cherry-pick-map from commits}
        0.1.1  02-Jun-2017  {Refactored & extra annotations. Edited cherry-picks}
        0.1.2  06-Jun-2017  {Modulised.  CHANGES.md & CREDITS.md now passed as args}
        0.1.3  08-Jun-2017  {More trello links & and adapted for repo under /scripts/changes-file}
        0.1.4  12-Jun-2017  {Temp fix for CALL/OUTPUT}
        0.1.5  14-Jun-2017  {Remove temp fix on CALL/OUTPUT}
        0.1.6  16-Jun-2017  {bug#nnnn marked meta with 'Fixed type and cc: accordingly}
    ]
    license: {Apache License 2.0} ;; Same as Rebol 3
    exports: [make-changes-file make-changes-block get-git-log]
]

;
; Globals
;

change!: make object! []

category!: make object! [
    Added:      make block! 0  ;; new feature
    Changed:    make block! 0  ;; changes (to existing functionality)
    Deprecated: make block! 0  ;; marked for removing in upcoming releases
    Fixed:      make block! 0  ;; bug fix
    Removed:    make block! 0  ;; removed feature
    Security:   make block! 0  ;; Security fix/change
]

release!: make object! [
    version: "Unreleased"
    date: _
    changes: make category! []
]

url: [
    ren-c:        https://github.com/metaeducation/ren-c/commit/
    rebol-issues: https://github.com/rebol/rebol-issues/issues/
]


;
; General functions
;

get-git-log: function [
    {Return Rebolised block of Ren/C `git log`}
][
    git-log: make string! 0
    call/shell/wait/output "git log --pretty=format:'[commit: {%h} author: {%an} email: {%ae} date-string: {%ai} summary: {%s}]'" git-log
    split git-log newline

]

make-changes-block: function [
    {Read in git-commit block, process notable changes returning Changes-block}
    commits [block!] {Rebolised git commit-log}
][
    block: reduce [make release! []]  ;; Changes-block

    add-change: function [
        {Add changes object to its correct place in Changes-block (by release > category type)}
        co [object!] {Changes object}
    ][
        category: select co 'type

        ;; new release?
        if attempt [v: co/version] [
            append block make release! [
                version: v
                date: co/date
                changes: make category! []
            ]
        ]    

        ;; append change to changes
        append block/(length? block)/changes/:category co
    ]

    for-each c commits [
        if notable? commit: load c [
            ;; add date
            ;append commit compose [date: (load first split commit/date-string space)]
            append commit compose [date: (12-12-2012)]

            add-change make change! commit
        ]
    ]

    block
]

parse-credits-for-authors: function [ 
    {Produces a block of author => [@github-name] mapping from CREDITS.md}
    ;; NB. used as switch in github-user-name func
    credits-file [file!] {CREDITS.md file}
    return: [block!]
][
    collect [
        keep [{Carl Sassenrath} [{@carls}]]

        parse to-string read credits-file [
            thru {Code Contributors}
            any [
                {**} copy author: to {**} {**} newline
                [{-} | {*}] space {[} copy github-name: to {](https://github.com/} (
                    keep compose/deep [(author) [(github-name)]]

                    ;; some cases use space trimmed author
                    keep compose/deep [(trim/all copy author) [(github-name)]]

                    ;; some cases github username was used for author
                    keep compose/deep [(next github-name) [(github-name)]]
                )
                | skip
            ]            
        ]
    ]
]


load-cherry-pick-map: does [map lock load %cherry-pick-map.reb]

notable?: function [
    {Is this a notable change?}
    c [block!] {Commit-log block}
    <has>
        cherry-pick (load-cherry-pick-map)
][
    numbers: charset "1234567890"

    ;; Let try and categorise type of commit (default is 'Changed)
    category: 'Changed
    parse text: c/summary [
        opt "* "
          ["Add" | "-add"]           (category: 'Added)
        | ["Fix" | "Patch" | "-fix"] (category: 'Fixed)
        | ["remove" | "delete"]      (category: 'Removed)
        | "Deprecate"                (category: 'Deprecated)
        | "Security"                 (category: 'Security)
    ]
    append c compose [type: quote (category)]

    ;; record any bug#NNNN or CC (CureCode) found
    cc: make block! 0
    parse text [
        any [
              "bug#" copy cc-num: some numbers (
                append c [type: 'Fixed]   ;; it's a bug fix!
                append cc to-integer cc-num
              )
            | ["cc " | "cc"]
              ["-" | "#"]
              copy cc-num: some numbers (append cc to-integer cc-num)
            | skip
        ]
    ]
    unless empty? cc [append c compose/only [cc: (cc)]]

    ;; if find commit in our cherry-pick map then apply logic / meta info
    if cherry-value: select cherry-pick c/commit [
        switch cherry-value [
            yes  [return true]       ;; This is a notable change so use (as-is)
            no   [return false]      ;; NOT notable so skip
        ]

        ;; so must be block?
        unless block? cherry-value [do make error! {Invalid rule in cherry-pick-map.reb}]

        ;; update commit block with rule info & return true (is notable)
        append c cherry-value
        return true
    ]

    ;
    ; so not been cherry-picked, so last checks for notability
    ;

    ;; if starts with "* " then it is notable
    if {* } = copy/part text 2 [return true]

    ;; If CureCode then it is notable
    unless empty? cc [return true]

    ;; this not a notable change
    false
]


;
; Make (new or overwrite) CHANGES.md
;

make-changes-file: procedure [
    {Make CHANGES.md file using Changes-block and template-CHANGES.md}
    changes-file  [file!]  {CHANGES file to create/overwrite}
    credits-file  [file!]  {CREDITS file to lookup github usernames}
    changes-block [block!] {Changes-block of release/category objects}
][
    template: split to-string read %template-CHANGES.md "!!!CHANGES!!!"
    changes: open/new changes-file
    authors: parse-credits-for-authors credits-file

    github-user-name: function [
        {Match Author in commit-log with github username in CREDITS.md. If not found return author}
        author [string!] {Author name in commit log}
    ][
        any [
            switch author authors
            switch trim/all copy author authors   ;; try again with space trimmed author
            author
        ]
    ]

    write-line: proc [s] [
        write changes join-all s
        write changes to-string newline
    ]

    md-link: func [s link] [
        join-all [{ [} s {](} link {)}]
    ]

    make-summary-text: function [
        {Make summary with github name and links}
        co [object!] {Change object}
    ][
        text: copy co/summary

        ;; remove any preceding * or - from summary
        if find [{* } {- }] copy/part text 2 [remove/part text 2]

        unspaced [
            {``` } text { ```} space

            ;; github username or git author name
            " *" github-user-name co/author "* | "

            ;; short commit hash
            md-link co/commit join-all [url/ren-c co/commit]

            ;; any related hash (cherry-pick collated)
            if related: select co 'related [
                map-each n related [
                    md-link co/commit join-all [url/ren-c co/commit]
                ]
            ]

            ;; show issues
            ;;if issues: select co 'issues [
            ;;   unspaced [sp form issues]
            ;;]

            ;; show CC issues
            ;; For now just list them
            if cc: select co 'cc [
                map-each n cc [
                    md-link join-all [{#CC-} n]  join-all [url/rebol-issues n]
                ]
            ]

            ;; wiki link
            if wiki: select co 'wiki [
                md-link "wiki" wiki
            ]

            ;; show trello link
            if trello: select co 'trello [
                md-link "trello" trello
            ]

        ]
    ]

    write-example: procedure [co] [
        if eg: select co 'example [
            write-line [{```rebol}]
            write-line [eg]
            write-line [{```}]
        ]
    ]

    write-change-text: proc [co] [
        write-line [{- } make-summary-text co]
        write-example co
    ]

    ;
    ; Write out new CHANGES file
    ;

    write changes template/1   ;; template top

    ;; summary text for each change
    for-each release changes-block [
        ;; ## [version]
        write-line either/only release/date [
            {## [} release/version {]}
            { - } release/date
        ][
            {## [} release/version {]}
        ]

        ;; ### Category (type)
        for-each type words-of release/changes [
            unless empty? release/changes/:type [
                write-line [{### } type]

                ;; - Change
                for-each co release/changes/:type [write-change-text co]
            ]
        ]
    ]

    write changes template/2   ;; template bottom
    close changes
]
