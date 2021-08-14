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
    url1: load-value "http://a.b.c/d?e=f%26"
    url2: load-value "http://a.b.c/d?e=f&"
    did all [
        not equal? url1 url2
        url1 == http://a.b.c/d?e=f%26
        url2 == http://a.b.c/d?e=f&
    ]
)

; Ren-C expands the delimiters that are legal in URLs unescaped
; https://github.com/metaeducation/ren-c/issues/1046
;
(
    b: load-value "[http://example.com/abc{def}]"
    did all [
        (length of b) = 1
        (as text! first b) = "http://example.com/abc{def}"
    ]
)

[#2380 (
    url: decode-url http://example.com/get?q=ščř#kovtička
    did all [
        url.scheme == 'http  ; Note: DECODE-URL returns BLOCK! with 'http
        url.user == null
        ^url.pass == '~no-user~
        url.host == "example.com"
        url.port-id == null
        url.path == "/get?q=ščř"
        url.tag == "kovtička"
    ]
)(
    url: decode-url http://švéd:břéťa@example.com:8080/get?q=ščř#kovtička
    did all [
        url.scheme == 'http
        url.user == "švéd"
        url.pass == "břéťa"
        url.host == "example.com"
        url.port-id == 8080
        url.path == "/get?q=ščř"
        url.tag == "kovtička"
    ]
)(
    url: decode-url http://host?query
    did all [
        url.scheme == 'http
        url.user == null
        ^url.pass == '~no-user~
        url.host == "host"
        url.port-id == null
        url.path == "?query"
        url.tag == null
    ]
)]

; There is logic in DECODE-URL which discerns IP addresses from hostnames and
; makes the IP addresses into TUPLE!, so you know not to do domain lookup
; on them.  It follows this rule from RFC-1738:
;
;    "The rightmost domain label will never start with a
;     digit, though, which syntactically distinguishes all
;     domain names from the IP addresses."
[(
    url: decode-url http://10.20.30.40:8000/this/is/an/ip?address
    did all [
        url.scheme == 'http
        url.user == null
        ^url.pass == '~no-user~
        tuple? url.host
        url.host == 10.20.30.40
        url.port-id == 8000
        url.path == "/this/is/an/ip?address"
        url.tag == null
    ]
)(
    url: decode-url http://10.20.30.40a:8000/this/is/a?hostname
    did all [
        url.scheme == 'http
        url.user == null
        ^url.pass == '~no-user~
        text? url.host
        url.host == "10.20.30.40a"
        url.port-id == 8000
        url.path == "/this/is/a?hostname"
        url.tag == null
    ]
)]
