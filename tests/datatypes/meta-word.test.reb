; The META-WORD! type is new and needs testing.

(any-word? '^foo)
("foo" = as text! '^foo)
(meta-word! = kind of '^foo)

(x: 10, (the '10) = ^x)
(x: null, null' = ^x)
