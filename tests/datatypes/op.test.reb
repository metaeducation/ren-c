; datatypes/op.r
(enfixed? '+)
(error? trap [enfixed? 1])
(action? get '+)

; #1934
(error? trap [do reduce [1 get '+ 2]])
(3 = do reduce [:+ 1 2])

