; %parse-validate.test.reb

(["abc" "def"] = parse [["abc" "def"]] [validate block! [some text!]])
~parse-mismatch~ !! (parse [['abc @def]] [validate block! [some text!]])
