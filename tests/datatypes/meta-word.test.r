; The ^WORD! type is new and needs testing.

(any-word? '^foo)
("foo" = as text! '^foo)
(meta-word! = type of '^foo)

(x: 10, (the '10) = lift x)
(^x: lift null, null = x)
