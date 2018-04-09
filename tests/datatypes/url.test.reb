; datatypes/url.r
(url? http://www.fm.tul.cz/~ladislav/rebol)
(not url? 1)
(url! = type of http://www.fm.tul.cz/~ladislav/rebol)
; minimum; alternative literal form
(url? #[url! ""])
(strict-equal? #[url! ""] make url! 0)
(strict-equal? #[url! ""] to url! "")
("http://" = mold http://)
("http://a%2520b" = mold http://a%2520b)

; Ren-C consideres URL!s to be literal/decoded forms
; https://trello.com/c/F59eH4MQ
; #2011
(
    url1: load "http://a.b.c/d?e=f%26"
    url2: load "http://a.b.c/d?e=f&"
    did all [
        not equal? url1 url2
        url1 == http://a.b.c/d?e=f%26
        url2 == http://a.b.c/d?e=f&
    ]
)
