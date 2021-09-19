; shell.test.reb
;
; The SHELL dialect offers a /INSPECT mode which returns what would be
; executed instead of running it.  That's used here to test.
;
; !!! The idea is extremely early in its development, so much has not been
; figured out.  It is being committed so it can gradually be improved.


; A basic concept of the shell dialect is that WORD!s are passed as-is
; with the spaces between them, in as natural a way possible.
;
; QUESTION: Should PATH!s and TUPLE!s also be treated "as-is" vs. normalized
; to the OS file convention?  Windows considers `dir /w` to have the /W as
; a command line switch, so it wouldn't want the `\W` reversed.  Is this
; the uncommon case that should be handled as `dir '/w` or similar?  The
; alternative is needing to use operators to indicate that it's a filename,
; which would produce code like `dir %% (base-dir)/*.txt`.
[
    ("ls -alF" = shell/inspect [ls -alF])
    ("ls -alF /usr/local" = shell/inspect [ls -alF /usr/local])
    ("ls -alF .travis.yml" = shell/inspect [ls -alF .travis.yml])
]


; FILE! allows platform-independent representation of paths, but this has
; to be converted to the host platform notation for the shell to use it.
; If the filename contains spaces it will be quoted (this should be extended
; to generally handling all escapable characters).
[
    (if system.version.4 = 3 [  ; Windows
        ("ls -alF foo\bar.txt" = shell/inspect [ls -alF %foo/bar.txt])
    ] else [
        ("ls -alF foo/bar.txt" = shell/inspect [ls -alF %foo/bar.txt])
    ])

    (if system.version.4 = 3 [  ; Windows
        ({ls -alF "foo\b ar.txt"} = shell/inspect [ls -alF %"foo/b ar.txt"])
    ] else [
        ({ls -alF foo/b ar.txt} = shell/inspect [ls -alF %"foo/b ar.txt"])
    ])
]


; Strings are taken literally, with their quotes included in shell code.
; Quotes are output regardless of whether braced strings are used or not.
; Internal quotes are escaped using C string literal rules
[
    ({ls -alF "/usr/local"} = shell/inspect [ls -alF "/usr/local"])
    ({"Some \"Quoted\" Text"} = shell/inspect [{Some "Quoted" Text}])
]


; GROUP! provides the mechanism for splicing in variables from Ren-C code.
[
    (
        options: '-alF
        "ls -alF" = shell/inspect [ls (options)]
    )
    (
        dir: '/usr/local
        "ls -alF /usr/local" = shell/inspect [ls -alF (dir)]
    )
    (
        file: '.travis.yml
        "ls -alF .travis.yml" = shell/inspect [ls -alF (file)]
    )
]


; Line continuation can be accomplished with `...`, either at the end
; of the line you are working on or the beginning of the next.
[
    ("one two^/three four^/five" = shell/inspect [one two
        three four
        five
    ])

    ("one two three four five" = shell/inspect [one two
        ... three four
        ... five
    ])
]
