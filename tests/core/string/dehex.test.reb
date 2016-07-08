; functions/string/dehex.r
["a%b" = dehex "a%b"]
["a%~b" = dehex "a%~b"]
["a^@b" = dehex "a%00b"]
["a b" = dehex "a%20b"]
["a%b" = dehex "a%25b"]
["a+b" = dehex "a%2bb"]
["a+b" = dehex "a%2Bb"]
["abc" = dehex "a%62c"]
