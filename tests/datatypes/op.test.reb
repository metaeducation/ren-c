; datatypes/op.r
(enfixed? '+)
(error? sys/util/rescue [enfixed? 1])
(action? get '+)

; #1934
(error? sys/util/rescue [do reduce [1 get '+ 2]])
(error? sys/util/rescue [do reduce [:+ 1 2]])
