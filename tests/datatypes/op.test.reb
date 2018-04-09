; datatypes/op.r
(enfixed? '+)
(error? try [enfixed? 1])
(function? get '+)

; #1934
(error? try [do reduce [1 get '+ 2]])
(3 = do reduce [:+ 1 2])

