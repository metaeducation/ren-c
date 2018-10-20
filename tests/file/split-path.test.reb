
([%./ _] == split-path %./)
([%../ _] == split-path %../)
([%./ %test] == split-path %test)
([%./ %test/] == split-path %test/)
([%test/ %test/] == split-path %test/test/)
