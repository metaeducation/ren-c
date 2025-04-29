; datatypes/email.r
(email? me@here.com)
(not email? 1)
(email! = type of me@here.com)
; "minimum"
(email? #[email! ""])
(equal? #[email! ""] make email! 0)
(equal? #[email! ""] to email! "")
