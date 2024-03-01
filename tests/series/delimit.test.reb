(null? delimit #" " [])
("1 2" = delimit #" " [1 2])

(null? delimit "unused" [])
("1" = delimit "unused" [1])
("12" = delimit "" [1 2])

("1^/^/2" = delimit #"^/" ["1^/" "2"])

;-- Empty text is distinct from BLANK/void
(" A" = delimit ":" [_ "A" void])
(":A:" = delimit ":" ["" "A" ""])
