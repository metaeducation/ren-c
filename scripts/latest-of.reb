Rebol [
    File: %latest-of.reb
    Date: [26-Mar-2019 24-Nov-2021]
    Version: 0.2.0

    Description: {
        Continuous integration builds of Windows, Mac, and Linux executables of
        Ren-C are placed on S3 storage.  This is done despite the fact that
        these builds are made to exercise build configurations.  Hence they are
        NOT "packaged" for any particular customer or usage.

        Some have decided to use these builds anyway.  :-/

        We generate a URL of the latest "greenlit" binary on S3 for a given
        platform.  In order to be greenlit a binary is only tested for very
        basic operation; starting up and shutting down successfully, and being
        able to do a successful HTTPS read.  Further testing is the
        responsibility of the committers at this time.
    }
    Usage: {
        ; Currently this is not implemented as a module; but script isolation
        ; means the only way you can get a function out is as a return result
        ; of the script.  We are reviewing the packaging options as the module
        ; system matures.

        >> latest-of: do <latest-of>

        ; An invocation with no arguments (or a blank argument) will assume you
        ; want the latest version of the currently running interpreter.

        >> latest-of
        == http://...

        ; Invocation with an OS_ID tuple only will get you the latest *debug*
        ; version of for that OS_ID (if it's in the CI builds).  Systems list:
        ;
        ;   https://github.com/metaeducation/ren-c/blob/master/tools/systems.r
        ;
        ; All stakeholders should run debug builds by default, unless you have
        ; a good reason not to!  Bug reports will not be processed unless the
        ; test has been reproduced under an instrumented build.

        >> latest-of 0.4.40  ; e.g. 64-bit-linux
        == http://...-debug....

        ; If you use /VARIANT you can ask for another variation, e.g. release
        ; should you have a *really* good reason.  (Please don't!  Help test!)

        >> latest-of/variant 0.3.1 'release  ; e.g. 32-bit Windows
        == http://...

        ; If you don't want the "latest" but just the build for a known commit,
        ; you can do this by using its short hash.

        >> latest-of/commit 0.4.40 "9d15d31"
        == http://...r3-9d15d31-...
    }
    Notes: {
      * This is intended to work in the Web REPL as well as the desktop builds,
        as both currently support READ and INFO? for URLs.  Note that only
        servers that enable "CORS" for a URL can be read by browsers.  (The
        S3 configuration we have for the builds has this enabled.)

      * Builds vary wildly by platform, 32-bit or 64-bit, hard float or soft
        float, emscripten binaryen/asyncify/pthreads...not to mention infinite
        combinations of extensions.  As already mentioned, the CI builds are
        done in a spread to serve the development process itself, not any
        particular use case.

        So right now the only choice you have is debug or release.  Depending
        on platform that may get you some features and not others.  ¯\_(ツ)_/¯

        Learn to read workflows and get involved if you have strong feelings!

          https://github.com/metaeducation/ren-c/tree/master/.github/workflows

        (There shouldn't be users of Ren-C yet, only *participating designers*,
        who all know how to build it and patch it.  So please avoid pushing
        expectations just because this script is allowed to exist. --HF)

      * In order to make sure this script stays working, it is tested by the
        greenlighting process itself.  Once a build is greenlit it makes sure
        the script will run and return the URL it just uploaded.  But note that
        this is subject to bootstrap problems...your interpreter may become out
        of date so it cannot run the current incarnation of %latest-of.reb - if
        you hit that, run LATEST-OF in the Web Repl instead of a desktop build.
    }
]


; Cloudfront does caching, and as such we don't want to use it to fetch the
; ever-changing greenlit hash.  But it's good for getting the binaries.
;
; !!! We use GitHub Actions now, and not Travis:
;
;    https://forum.rebol.info/t/goodbye-travis-but-its-not-all-despair/1421
;
; However, the folder's name has not been changed.  It should be renamed to
; something service-agnostic (like "nightlies" or "ci-builds") at some point.
;
s3root: https://metaeducation.s3.amazonaws.com/travis-builds/  ; sees updates
cloudroot: https://dd498l1ilnrxu.cloudfront.net/travis-builds/  ; perma-cached


build-variants: [debug release]


latest-of: func [
    {Get a link to the latest S3 stored build of the interpreter}

    return: [url!]

    os "https://github.com/metaeducation/ren-c/blob/master/tools/systems.r"
        [<end> blank! tuple!]
    /variant "Which build variation (DEBUG, RELEASE)"
        [word!]
    /commit "Link for specific commit number (defaults to latest commit)"
        [text!]
    /verbose "Print file size, commit, hash information"
][
    === DEFAULT OS TO VERSION RUNNING THIS SCRIPT (OR OS RUNNING BROWSER) ===

    ; If you are running the web build and don't ask for a variant, the most
    ; sensible thing to do is to try and give you a version matching your
    ; browser's platform...since giving you a link to the Web Repl is useless.
    ;
    ; There's no perfect way to do this.  But if you don't like the answer you
    ; get, pass in an OS_ID explicitly.  It's just a convenience.
    ;
    ; https://stackoverflow.com/a/38241481
    ; https://stackoverflow.com/q/1741933
    ;
    if (not os) and (system.version.4 = 16) [  ; web build
        let platform: js-eval {
            var userAgent = window.navigator.userAgent
            var platform = window.navigator.platform

            if (/Android/.test(userAgent))
                platform = 'Android'
            else if (/Linux/.test(platform))
                platform = 'Linux'
            else if (userAgent.indexOf("WOW64") != -1)
                platform = 'Win64'  // may be 32 bit browser on 64 bit platform
            else if (userAgent.indexOf("Win64") != -1)
                platform = 'Win64'  // may be 32 bit browser on 64 bit platform

            platform
        }
        os: switch platform [
            ; "Mac68K"
            ; "MacPPC" - Is this 0.2.1 (pre OS X) or OS X PPC ?
            "MacIntel" [0.2.40]
            "Macintosh" [0.2.40]  ; What do current macs say?

            "Win32" [0.3.1]
            "Win64" [0.3.40]
            "Windows" [0.3.40]
            ; "WinCE" - We haven't built this in a long time

            "Linux" [0.4.40]  ; Could we tell 32 vs. 64 bit?  Assume 64.

            ; !!! We need a way to specify that you want the latest binary
            ; vs. the latest installer, e.g. an .APK file.  Getting the Bionic
            ; executable file may seem useless to the average user, but it's
            ; interesting in building things like the .APK itself.
            ;
            "Android" [0.13.1]

            ; "iPhone"
            ; "iPad"
            ; "iPod"

            fail [
                "Browser-detected platform" mold os "unavailable in CI builds."
                "Please pass a supported OS_ID explicitly to LATEST-OF"
            ]
        ]
    ]

    ; Otherwise, we're in a desktop build and can just use the version tuple
    ; built into the interpreter as a default.
    ;
    os: default [
        ; If we are defaulting the OS from the current interpreter, default
        ; the debug or release status also.  TEST-LIBREBOL is only available
        ; in debug builds, and should not print anything (there should be a
        ; better detection...)
        ;
        variant: default [
            trap [test-librebol] then ['release] else ['debug]
        ]
        to tuple! reduce [0 system.version.4 system.version.5]
    ]
    if 3 <> length of os [
        fail ["OS specification must be 3 digit tuple:" os]
    ]
    if 0 <> first os [  ; so far, all start with 0
        fail ["First digit of OS specification tuple must be 0:" os]
    ]

    let extension: if (find/only [0.3.1 0.3.40] os) [".exe"]

    ; !!! Though the OS is represented as a tuple, non-Rebol scripts treat
    ; them as strings...so `0.3.01` is not canonized to `0.3.1`.  The S3
    ; directories are currently impacted by that:
    ;
    ; https://forum.rebol.info/t/tuple-troubles-version-0-3-01-vs-0-3-1/1755
    ;
    ; We want to stamp this out.  But until we do...turn the OS tuple into a
    ; string representation with a leading 0 in the third slot, if needed.
    ;
    let opt-zero: if os.3 < 10 ["0"]
    os: unspaced [os.1 "." os.2 "." opt-zero os.3]


    === DEFAULT VARIANT TO DEBUG ===

    ; All stakeholders are requested to use debug builds unless they have a
    ; really good reason not to.  The term "checked" may wind up preferred to
    ; "debug", so begin thinking about supporting that:
    ;
    ; https://forum.rebol.info/t/debug-builds-vs-checked-builds/1708

    variant: default ['debug]
    if variant = 'checked [
        variant: 'debug
    ]
    if not find/case/only build-variants variant [
        fail ["Build variant must be one of:" mold build-variants]
    ]
    let suffix: if variant <> 'release [join "-" variant]


    === DEFAULT COMMIT TO THE LAST GREEN-LIT HASH ===

    ; After a build is validated (and verified as being a completed upload), a
    ; file called %last-deploy.short-hash is updated in the build directory.
    ; This what we use by default if an explicit commit is not provided.
    ;
    ; Because this file is overwritten to reflect where to find the latest
    ; binary, we do -not- want to get this file from cloudfront.  Use S3, so
    ; we get the latest data that was written at that location--not a cache.
    ; (Invalidating cloudfront caches is possible, but you get charged for it.)
    ;
    ; Note: Current interpreter may or may not have a commit built into it.
    ; If it has one, the short hash is `copy/part system.commit 7`

    commit: default [
        let last-deploy: to url! unspaced [
            s3root os %/last-deploy.short-hash  ; don't use cloudfront, use S3!
        ]
        trim/tail as text! read last-deploy
    ]


    === TRY TO READ THE METADATA VIA HTTP HEAD REQUEST FOR EXPECTED URL ===

    ; Rather than just return the expected URL, we test to see that the file
    ; actually does exist.  Here we can use cloudfront, because every build
    ; has its commit shorthash in the URL...so there aren't caching problems
    ; with files being overwritten (at least not typically).

    let filename: to file! unspaced ["r3-" commit suffix extension]
    let url: to url! unspaced [
        cloudroot os "/" filename  ; cached cloudfront is okay for the binary
    ]

    if verbose [
        print ["Verifying:" url]
    ]

    let info: info? url  ; If this fails, the build is not available

    if verbose [
        print ["File size:" (round/to divide info.size 1000000 0.01) "Mb"]
        print ["Date:" any [info.date, "<unknown>"]]
    ]

    return url
]
