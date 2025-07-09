; %source-comment.test.reb
;
; Test for semicolon-based comments.  One key difference in Ren-C with these
; comments is that a comment must have space between it and another token,
; otherwise the semicolon is considered part of the token:
;
;     >> #; ; comment after a one-codepoint rune for `;`
;     == #;
;
;     >> # ; comment after a zero-codepoint rune
;     == #
;
;     >> 'a ; comment after a quoted word
;     == 'a
;
;     >> 'a; illegal quoted word
;     ** Syntax Error: invalid "word" -- "a;"
;
; This differs from Rebol2 and Red.

(
    rune: transcode:one "#; ; comment after a one-codepoint rune"
    all [
        rune? rune
        1 = length of rune
        59 = codepoint of rune
        ";" = to text! rune
    ]
)(
    rune: transcode:one "# ; comment after a zero-codepoint rune"
    all [
        rune? rune
        1 = length of rune
        32 = codepoint of rune
        " " = to text! rune
    ]
)

(
    q-word: transcode:one "'a ; comment after a quoted word"
    all [
        quoted? q-word
        (first [a]) = unquote q-word
        'a = unquote q-word
        "a" = to text! unquote q-word
    ]
)(
    'scan-invalid = (rescue [load "'a; illegal quoted word"]).id
)

; Semicolons are technically legal in URL (though many things that auto-scan
; code to find URLs in text won't include period, semicolon, quotes...)
(
    url: transcode:one "http://abc;"
    http://abc; = url
)

(
    b: load ";"
    all [
        b = []
        not new-line? b
    ]
)

([a] = transcode "a ;")
(
    data: transcode "a ;^/a"
    all [
        data = [a a]
        not new-line? data
        new-line? next data
        not new-line? next next data
    ]
)
