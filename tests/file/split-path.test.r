
(
    [dir file]: split-path %./
    [%./ ~null~] = reduce [dir, reify file]
)
(
    [dir file]: split-path %../
    [%../ ~null~] = reduce [dir, reify file]
)
(
    [dir file]: split-path http://rebol.com/index.html
    [http://rebol.com/ %index.html] = reduce [dir file]
)


; These tests are derived from a discussion on the Red GitHub issues:
;
; https://github.com/red/red/issues/5024
;
; Review what the best design is as time permits.  It is nice to know when
; the components are not there, but the loss of a datatype (e.g. null vs. %"")
; may complicate the result when trying to join back.
(
    split-path-tests: compose:deep [
        %foo                            [_ %foo]
        %""                             [_ _]
        %/c/rebol/tools/test/test.r     [%/c/rebol/tools/test/ %test.r]

        ; Historical Rebol and Red will return a directory for the file part
        ; of a split path if there is no file part.  I agree with @hiiamboris
        ; that this is undesirable and it's better to get the accurate
        ; directory and no file part in these cases.
        ;
        ; See: https://github.com/red/red/issues/5024#issuecomment-1006032330
        ;
        %/c/rebol/tools/test/           [%/c/rebol/tools/test/ _]
        %/c/test/test2/                 [%/c/test/test2/ _]
        %//                             [%// _]
        %/c/                            [%/c/ _]
        http://www.rebol.com/           [http://www.rebol.com/ _]
        http://                         [http:// _]

        ; By convention, all directories must end in slash in Ren-C, and this
        ; includes the special . and .. directories.  This is the correct form.
        ;
        %./                             [%./ _]
        %../                            [%../ _]

        ; Ren-C disallows filenames of . or .. so if they are to be used as
        ; a directory they must have terminal slash.  SPLIT-PATH returns an
        ; error on these cases if you don't use the :RELAX refinement, but
        ; we use it here in case the client wants to handle it.
        ;
        %.                              [_ %.]
        %..                             [_ %..]
        %./.                            [%./ %.]
        %../..                          [%../ %..]
        %foo/..                         [%foo/ %..]
        %foo/.                          [%foo/ %.]
        %foo/../.                       [%foo/../ %.]
        http://..                       [http:// %..]
        http://.                        [http:// %.]
        http://../.                     [http://../ %.]

        %/c/rebol/tools/test            [%/c/rebol/tools/ %test]
        %/c/test/test2/file.x           [%/c/test/test2/ %file.x]
        %/c/test/test2                  [%/c/test/ %test2]
        %/c/test                        [%/c/ %test]
        %//test                         [%// %test]
        %/test                          [%/ %test]
        %/                              [%/ _]
        %../../test                     [%../../ %test]
        %foo/../bar                     [%foo/../ %bar]
        %foo/./bar                      [%foo/./ %bar]
        %/c/foo/../bar                  [%/c/foo/../ %bar]
        %/c/foo/./bar                   [%/c/foo/./ %bar]
        http://www.rebol.com/index.html [http://www.rebol.com/ %index.html]
        http://www.rebol.com            [http:// %www.rebol.com]
        http://../bar                   [http://../ %bar]
        http://./bar                    [http://./ %bar]
        (at %/vol/dir/file.r 6)         [%dir/ %file.r]
    ]
    for-each [test result] split-path-tests wrap [
        [dir file]: split-path:relax test
        if result <> actual: reduce [any [dir _], any [file _]] [
            panic [mold test 'expected mold result "but got" mold actual]
        ]
        res: if file and dir [
            join dir file
        ] else [
            any [file, dir, %""]
        ]
        if test <> res [
            panic ["Split did not JOIN equivalently:" mold test mold res]
        ]
    ]
    ok
)
