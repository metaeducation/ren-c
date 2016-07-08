; datatypes/url.r
[url? http://www.fm.tul.cz/~ladislav/rebol]
[not url? 1]
[url! = type-of http://www.fm.tul.cz/~ladislav/rebol]
; minimum; alternative literal form
[url? #[url! ""]]
[strict-equal? #[url! ""] make url! 0]
[strict-equal? #[url! ""] to url! ""]
["http://" = mold http://]
["http://a%2520b" = mold http://a%2520b]
