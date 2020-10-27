> These tests originated from the repository [on GitHub][1].  It was originally the work of Carl Sassenrath, Ladislav Mecir, Andreas Bolka, Brian Hawley, and John K.
>
> The Ren-C fork was moved from a submodule directly into the %tests/ subdirectory.  This is because development on the original repository ceased, and it makes keeping the tests in sync with the Ren-C version easier.
>
> In the Ren-C version, an extensive source analysis script was added by Brett Handley.  This has parse rules for C and is used to check the source codebase itself for static analysis properties.
>
> The test suite itself as well as the test framework are Licensed under the [Apache License, Version 2.0][2].
>
> * Copyright 2012 REBOL Technologies
> * Copyright 2013 Saphirion AG
> * Copyright 2013-2017 Ren-C Open Source Contributors
>
> This README was originally written by Ladislav Mecir.

[1]: https://github.com/rebolsource/rebol-test
[2]: http://www.apache.org/licenses/LICENSE-2.0


# Running the Tests

The test cases are listed in `core-tests.r` and the script for running them is called `run-recover.r`.  Executing it on Kubuntu might look like:

    ladislav@lkub64:/rebol-test$ /r3/make/r3 run-recover.r

If the program crashes (either the test framework or the interpreter), just run the tests again the same way as before.  The crash will be noticed and the log will pick up from the previous point.

*(Note: Although the original core-tests contained cases that were known to crash or fail, the baseline for the Ren-C fork is a zero-tolerance for crashes or failures.  It should therefore not be considered normal for a crash necessitating recovery to occur, and patches should not be submitted if the tests cannot pass.)*


# Log Files

The tests in the log file are always text-copies of the tests from the test file, which means that they are not modified in any way. It is possible to run them in REBOL console as well as to find them using text search in the test file if desired.

A run-recover.r result in Kubuntu running a build of the 2.101.0.4.4 interpreter might be named:

    r_2_101_0_4_4_F9A855_E85A1B.log

The first character of the log file name, #"r" is common to all run-recover log files. The next part describes the version of the interpreter, the following 6 characters are a part of the interpreter executable checksum, and the last 6 characters preceding the file extension are a part of the core-tests.r file checksum.

*(Note: The test framework needs a path to the interpreter executable to be able to calculate the interpreter checksum.  If the full path to the interpreter executable isn't obtained from the command line, the argument of the run-recover.r script will be used as the path to the executable.  Otherwise, the system/build variable will be used.)*


# Test File Format

The test file format was originally designed by Carl Sassenrath to be Rebol compatible, and as simple as possible.  Here are some tests cases for the closure! datatype, notice that only some of them are marked as `<r3only>`, suggesting they are meant just for the R3 interpreter:

    ; datatypes/closure.r
    (closure? closure [] ["OK"])
    (not closure? 1)
    <r3only>
    (closure! = type? closure [] ["OK"])
    ; minimum
    (closure? closure [] [])
    ; literal form
    <r3only>
    (closure? first [#[closure! [[] []]]])

The Ren-C project has modified the original block test format by Carl to
support grouped tests. Test groups are encoded as BLOCK! while individual
tests are encoded as GROUP!. References to issues (bugs) are encoded as ISSUE!
within the test group's BLOCK!. Here is a sample of parse tests, note that the
grouped test here only contains one test vector:

    (parse "abcd" [to 5])
    (parse "abcd" [to 128])

    [#1965
        (parse "abcd" [thru 3 "d"])
    ]

Despite its Rebol appearance, the test file is not LOAD-ed in its entirety by the interpreter.  This complicates test file parsing a bit, but it brings significant advantages:

- An individual "malformed" test will not cause all tests to fail.

- A single test file can be used to test different (more or less source-code compatible) interpreters, every one of them having a different "idea" what "Rebol" is.

- One of the properties that can be tested is the ability of the interpreter to load the test as Rebol code.

- Since the test file is handled by the test framework as a text file having the format described below, the test framework is able to always record/handle the original "look" of the tests.  Therefore, the original tests cannot be "distorted" in the log by any incorrect LOAD/MOLD transformation performed by the interpreter.

- Tests "stand for themselves" not needing any names. (Test writers can use whatever naming convention they prefer, but names are not required for the test framework to be able to handle the tests.)


# Test Dialect

### Test cases

Test cases have to be enclosed in properly matched square brackets

A test is successful only if it can be correctly loaded and it yields LOGIC! TRUE when evaluated.
When writing tests to meet this requirement, DID and NOT are useful, for example:

    did find "x" #"x"

Breaks, throws, errors, returns, etc. leading out of the test code are detected and marked as test failures.  The test framework is built in such a way that it can recover from any kind of crash and finish the testing after the restart.

### Comments

Comments following the semicolon character until the end of the line are allowed.

### Flags

Tags are used to signal special handling of the test. They are handled by the environment as flags excluding the marked test from processing. Only if all flags used are in the set of acceptable flags, the specific test is processed by the environment, otherwise, it is skipped.

Tags are used to indicate a special character of tests. For example,

    <64bit>

...indicates that the test is meant to be used only in 64-bit builds. Any test may be marked by as many flags as desired.

Flags restrict the usage of tests. If the DO-RECOVER function is called without a specific flag being mentioned in the FLAGS argument, all tests marked using that flag are ignored. For example, if the above <64bit> flag is not mentioned in the FLAGS argument, no <64bit> test is run.

Currently available flags are:

    ; the flag influences only the test immediately following it,
    ; if not explicitly stated otherwise

    <32bit>
    ; the test is meant to be used only when integers are 32bit

    <64bit>
    ; the test is meant to be used only when integers are 64bit

*(Note: Originally flags existed for selecting if tests ran in R3-Alpha vs. Rebol2, but these were removed in the Ren-C fork.)*

### Files/URLs

Files or URLs specify what to include, i.e., they allow a file to contain references to other test files.

*(Note: Despite the existence of this feature, the core-tests were created as a giant monolithic file.  @HostileFork didn't even know this feature existed because it wasn't used.  If this test suite's methodology is to be used going forward, then certainly multiple files must be used!)*


# Summary

The summary (can be found at the end of the log file), e.g.:

    system/version: 2.7.8.3.1
    interpreter-checksum: #{1DEF65DDE53AB24C122DA6C76646A36D7D910790}
    test-checksum: #{E85A1B2945437E38E7654B9904937821C8F2FA92}
    Total: 4598
    Succeeded: 3496
    Test-failures: 156
    Crashes: 7
    Dialect-failures: 0
    Skipped: 939

...in the former case and

    system/version: 2.101.0.4.4
    interpreter-checksum: #{F9A855727FE738149B8E769C37A542D4E4C8FF82}
    test-checksum: #{E85A1B2945437E38E7654B9904937821C8F2FA92}
    Total: 4598
    Succeeded: 4136
    Test-failures: 142
    Crashes: 15
    Dialect-failures: 0
    Skipped: 305

...in the latter.

The test-checksums and the total number of the tests are equal. That is because we used the same version of the tests.

However, the numbers of succeeded tests, failed tests, crashing tests, and skipped tests differ.

The reason why the number of skipped tests differs is that 2.7.8 is R2 while 2.101.0 is R3. These interpreter versions are different in many aspects and it does not make sense to perform some R2 tests in the R3 environment and vice versa, which leads to the necessity to skip some tests depending on the interpreter type.

The "Dialect failures" number counts the cases when the test framework found incorrectnesses in the test file, cases when the test file was not written in accordance with the formatting rules described below.

If you get more than zero dialect failures, you should correct the respective test file.

The test environment counts successful tests, failed tests, crashing tests, skipped tests, and test dialect failures, i.e., the cases when the test file is not properly formatted.

Files or URLs in the test file "outside" of tests are handled as directives for the test environment to process the tests in the respective file as well.


# Filtering test logs

Sometimes we are not interested in all test results preferring to see only a list of failed tests. The log-filter.r script can be used for that as follows:

    e:\Ladislav\rebol\rebol-view.exe log-filter.r r_2_7_8_3_1_1DEF65_E85A1B.log

The result is the file:

    f_2_7_8_3_1_1DEF65_E85A1B.log

i.e., the file having a prefix #"f", otherwise having the same name as the original log file and containing just the list of failed tests.


# Comparing test logs

We have seen that we obtained different test summaries for different interpreter versions. There is a log-diff.r script allowing us to obtain the list and summary of the differences between two log files.

The `log-diff.r` script can be run as follows:

    e:\Ladislav\rebol\rebol-view.exe log-diff.r r_2_7_8_3_1_1 DEF65_E85A1B.log r_2_101_0_4_4_F9A855_E85A1B.log

The first log file given is the "old log file" and the second file is "new log file".

The result is the `diff.r` file containing the list of the tests with different results and the summary as follows:

    new-successes: 907
    new-failures: 25
    new-crashes: 4
    progressions: 119
    regressions: 94
    removed: 302
    unchanged: 3147
    total: 4598

Where, again, we see that the total number of tests was 4598.

- **new-successes**: how many successful tests were newly performed (performed in the new log, but not performed in the old log)

- **new-failures**: how many failing tests were newly performed

- **new-crashes**: how many crashing tests were newly performed

- **progressions**: how many tests have improved results

- **regressions**: how many tests have worse results than before

- **removed**: how many tests are not performed in the new log

- **unchanged**: how many tests have the same result both in the old and in the new log

The log difference is useful if for knowing the effect of an interpreter code update. In this case, it is most convenient (but not required) to perform the same test suite in both the old as well as the new interpreter version.

The difference can also be used to find the effect of test suite changes. In this case, it is most convenient (but not required) to perform both the old and new test suite version using the same interpreter and compare the logs.
