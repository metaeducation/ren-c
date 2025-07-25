![Ren-C Logo][100]

# Ren-C

[**Ren-C**][1] is a *deeply* redesigned [LGPL 3.0-licensed][2] derivative of
the [Rebol 3][3] [codebase][4].  It explores solutions to some of the Rebol
language's longstanding open questions, adding fundamental new evaluation
abilities and API embeddings.

[1]: https://github.com/metaeducation/ren-c
[2]: https://www.gnu.org/licenses/lgpl-3.0.html
[3]: https://en.wikipedia.org/wiki/Rebol
[4]: https://github.com/rebol/rebol

While Rebol 3 built for many platforms, Ren-C extends those to everything from
[OpenBSD to HaikuOS and WebAssembly][5].  But the experimental nature of the
project and limited resources mean there isn't support for packaging and
distribution of native binaries.  So the table stakes for participating is
building your own native interpreter (see instructions below)

[5]: https://github.com/metaeducation/ren-c/blob/master/tools/platforms.r

The current sole focus for deploying a *prebuilt* experience to users is via
WebAssembly in the web browser.  See the [demo of the Web Console][6]
that was shown at the Rebol 2019 Conference.

[6]: https://youtu.be/PT3GOe1pj9I?t=407

*(A [more conservative evolution][7] of the R3-Alpha codebase is maintained by
user @Oldes, and may interest some people who don't want to run on the web.)*

[7]: https://github.com/Oldes/Rebol3


## API

One major enabling feature of Ren-C is that it has a "user-friendly" API for
C and JavaScript, which uses novel tricks to compose code as mixtures of
strings and spliced Rebol values:

    int x = 1020;
    Value* negate = rebValue("get $negate");  // runs code, returns value

    rebElide("print [", rebI(x), "+ (2 *", rebRUN(negate), "358)]");

    // Would print 304--e.g. `1020 + (2 * -358)`, rebElide() returns C void.

The way this can work is described in another talk from Rebol 2019,
entitled ["Abusing UTF-8 For Fun and Profit"][8]

[8]: https://www.youtube.com/watch?v=6nsKTpArTCE

Beyond the API and Web Build, improvements to the language itself *range in
the hundreds*.  They are ever-evolving but are tracked periodically on the
[Trello board][9] and posts on the forum.

[9]: https://trello.com/b/l385BE7a/rebol3-porting-guide-ren-c-branch


## Community

The best way to get acquainted with all that is going on would be to
[**Join The Forum!**][10]  Feel free to post in the [Introductions Category][11]
and ask anything you would like.

[10]: https://rebol.metaeducation.com/
[11]: https://rebol.metaeducation.com/c/introductions

It's also possible to contact the developers via [the GitHub Issues][12].
*(Ren-C inherited Rebol's thousands-strong issue database, so there's a
lifetime's worth of design points to think about!)*

[12]: https://github.com/metaeducation/rebol-issues/issues


## Name

The "Ren-C" name comes from the idea that it is a C implementation of the
"REadable Notation" (a name given to Rebol's file format).  The codebase is
able to compile as ANSI C99, despite using a wide spectrum of static analysis
enhancements that apply if built as C++.

Long term, it is not intended to be the name of a language.  It's simply a
core that could be packaged and configured by other "branded" distributions,
such as Rebol itself.


## Language Dependencies: C99 (or higher), C++11 (or higher)

The baseline for building the Ren-C interpreter itself is C99.  The features
it uses that were not defined in C89 are:

    * __VA_ARGS__ variadic macros,
    * double-slash comments
    * declaring variables in the middle of functions
    * declaring variables in the conditions of for loops

(Many C89-era compilers could do these things before they were standards, so
there's a possibility that Ren-C can be built by pre-C99 systems.)

While the interpreter sources need C99, the API can be used by C89 client
code.  The calls will be slightly uglier, due to the lack of variadic macros
to take care of some of the boilerplate.  But it should work.

If you use C++11 to build the sources, then this adds many extremely useful
compile-time checks.  Anyone developing and submitting patches to the system
should make sure their changes will compile with C++.

(C++98 support was included for a while, but it lacks <type_traits> and
other features which are required to make the C++ build of any real use
beyond what C provides.  So support for C++98 was ultimately dropped, and
if you don't have a C++11 compiler you should just use C99.)


## Building

The system does not require GNU Make, CMake, or any other make tools.  It only
needs a copy of a Ren-C executable to build itself.  To do a full build, it
can just invoke a C compiler using [the CALL facility][13], with the
appropriate command lines.

[13]: http://www.rebol.com/docs/shell.html

Several platforms are supported, including Linux, Windows, OS X, Android, and
support for JavaScript via WebAssembly.  Configurations for each platform are
in the %configs/ directory.  When the build process is run, you should be in
the directory where you want the build products to go (e.g. %build/).  Here
is a sample of how to compile under Linux:

    # You need a Ren-C-based Rebol to use in the make process
    # See %tools/bootstrap-shim.r regarding what versions are usable
    # Currently there are usable executables in %/prebuilt ...
    # ...but that's not a permanent solution!
    #
    ~/ren-c$ export R3_MAKE="$(pwd)/prebuilt/r3-linux-x64-8994d23"

    ~/ren-c$ cd build

    ~/ren-c/build/$ "$R3_MAKE" ../make.r \
        config: ../configs/default-config.r \
        debug: asserts \
        optimize: 2

For a list of options, run %make.r with `--help`.

Though it does not *require* other make tools, it is optional to generate a
`makefile` target, since %make.r takes parameters like `target: makefile`.
But there are several complicating factors related to incremental builds, due
to the fact that there's a large amount of C code and header files generated
from tables and scans of the source code.  If you're not familiar with the
source and what kinds of changes require rebuilding which parts, you should
probably do full builds.

As a design goal, compiling Ren-C requires [very little beyond ANSI C89][14].
Attempts to rein in compiler dependencies have been a large amount of work,
and it still supports a [number of older platforms][15].  However, if it is
compiled with a C++ compiler then there is significantly more static analysis
at build time, to catch errors.

[14]: https://rebol.metaeducation.com/t/on-building-ren-c-with-c-compilers/1343
[15]: https://github.com/metaeducation/ren-c/blob/master/make/tools/platforms.r

*(Note: The build process is [far more complicated than it should be][16], but
other priorities mean it isn't getting the attention it deserves.  It would be
strongly desirable if community member(s) could get involved to help
streamline and document it!  Since it's now *all* written in Rebol, that
should be more possible--and maybe even a little "fun" (?))*

[16]: https://rebol.metaeducation.com/t/new-build-executables-new-build-strategy/1432


## License

When Rebol was open-sourced in 2012, it was [licensed as Apache 2.0][17].
Despite the Ren-C team's belief in [Free Software Foundation's principles][18],
contributions were made as Apache 2.0 up until 2020, to make it easier for
code to be taken back to the Rebol GitHub or other branches.

[17]: http://www.rebol.com/cgi-bin/blog.r?view=0519
[18]: https://www.gnu.org/philosophy/shouldbefree.en.html

Due to limited cases of such any take over an eight-year span, the Ren-C
license was [changed to the Apache-2-compatible LGPL 3][18].

[19]: https://rebol.metaeducation.com/t/ren-c-license-changed-to-lgpl-3-0/1342

The current way to explore the new features of Ren-C is using the `r3`
console.  It is *significantly* enhanced from the open-sourced R3-Alpha...with
much of its behavior coming from [userspace Rebol code][20] (as opposed to
hardcoded C).  In addition to multi-line editing and UTF-8 support, it
[can be "skinned"][21] and configured in various ways, and non-C programmers
can easily help contribute to enhancing it.

[20]: https://github.com/metaeducation/ren-c/blob/master/src/os/host-console.r
[21]: https://github.com/r3n/reboldocs/wiki/User-and-Console



[100]: https://raw.githubusercontent.com/metaeducation/ren-c/master/docs/ren-c-logo.png
