; datatypes/email.r
(email? me@here.com)
(not email? 1)
(email! == type of me@here.com)
; "minimum"
(email? #[email! ""])
(strict-equal? #[email! ""] make email! 0)
(strict-equal? #[email! ""] to email! "")
