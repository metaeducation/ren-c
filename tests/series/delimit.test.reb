("" == delimit [] #" ")
("1 2" == delimit [1 2] #" ")

("" == delimit [] "unused")
("1" == delimit [1] "unused")
("12" == delimit [1 2] "")

("1^/^/2" == delimit ["1^/" "2"] #"^/")
