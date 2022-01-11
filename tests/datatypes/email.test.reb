; datatypes/email.r
(email? me@here.com)
(email? graham.chiu@mdh.health.nz)
(not email? 1)
(email! = type of me@here.com)
; "minimum"
(email? #[email! ""])
(strict-equal? #[email! ""] make email! 0)
(strict-equal? #[email! ""] to email! "")

; There are some complex rules determining what an email address can and can't
; be like.  e.g. dots are legal in the part to the left of the @, but not two
; dots in a row.
;
; ref: http://codefool.tumblr.com/post/15288874550/list-of-valid-and-invalid-email-addresses
;
; Weird email addresses are not a priority for the Ren-C project's near-term
; goals, but dots in emails were considered important so that needed to be
; supported (it was broken when dots-for-generic tuples were added into the
; scanner, and had to be hacked back in).

(for-each [supported text] [

    ; Valid email addresses (that aren't considered "strange").

    + {email@example.com}
    + {firstname.lastname@example.com}
    + {email@subdomain.example.com}
    + {firstname+lastname@example.com}
    + {email@123.123.123.123}
    - {email@[123.123.123.123]}
    - {"email"@example.com}
    + {1234567890@example.com}
    + {email@example-one.com}
    + {_______@example.com}
    + {email@example.name}
    + {email@example.museum}
    + {email@example.co.jp}
    + {firstname-lastname@example.com}

    ; "Strange" but Valid Email Addresses

    - {much.”more\ unusual”@example.com}
    - {very.unusual.”@”.unusual.com@example.com}
    - {very.”(),:;<>[]”.VERY.”very@\\ "very”.unusual@strange.example.com}
][
    assert [find [+ -] supported]
    trap [
        email: load-value text
        type: type of email
        if type <> email! [
            fail ["LOAD of" mold text "should've been email but was" mold type]
        ]
    ] then e -> [
        if supported = '+ [
            fail ["Should LOAD" mold text "as an email but couldn't"]
        ]
    ]
], true)


; List of Invalid Email Addresses
;
; plainaddress
; #@%^%#$@#$@#.com
; @example.com
; Joe Smith <email@example.com>
; email.example.com
; email@example@example.com
; .email@example.com
; email.@example.com
; email..email@example.com
; あいうえお@example.com
; email@example.com (Joe Smith)
; email@example
; email@-example.com
; email@example.web
; email@111.222.333.44444
; email@example..com
; Abc..123@example.com
;
; List of Strange Invalid Email Addresses
;
; ”(),:;<>[\]@example.com
; just”not”right@example.com
; this\ is"really"not\allowed@example.com
