; %parse-the.test.reb
;
; Historical PARSE had a QUOTE operation that you could use to match things
; literally.  It needed it because not all types had a LIT-XXX! form.
;
; With generic quoting it is less necessary.  But especially when quoting
; counts are involved, some people may prefer `parse [''x] [the ''x]` to
; `parse [''x] ['''x]`.  Or if quotes are part of the name of the variable
; it might read better, e.g. `parse [x: result'] [set-word! the result']`

[
    (uparse? [wb] [the wb])
    (uparse? [123] [the 123])
    (uparse? [3 3] [2 the 3])
    (uparse? [blank] [the blank])
    (uparse? [some] [the some])
]

[#1314 (
    d: [a b c 1 d]
    true
)(
    'd = uparse d [thru the 1 'd]
)(
    1 = uparse d [thru 'c the 1 elide 'd]
)]
