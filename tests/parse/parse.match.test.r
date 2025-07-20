; %parse.match.test.r
;
; MATCH is run implicitly by things like INTEGER! or EVEN?/

(304 = parse [1020 <foo> 304] [some match [tag! even?]])
(304 = parse [1020 <foo> 304] [some match ([tag! even?])])

~parse-incomplete~ !! (parse [1020 <foo> 403] [some match [tag! even?]])
