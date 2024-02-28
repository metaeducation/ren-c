![Ren-C Logo][100]

# Ren-C
[![Build Status][101]](https://travis-ci.org/metaeducation/ren-c)


**Ren-C** is an interim fork of the [Apache 2.0 open-sourced][1] [Rebol][2] codebase.

[1]: http://www.rebol.com/cgi-bin/blog.r?view=0519
[2]: https://en.wikipedia.org/wiki/Rebol

The goal of the project isn't to be a "new" language, but to solve many of the outstanding
design problems historically present in Rebol.  Several of these problems have been solved
already.  For progress and notes on these issues, a [Trello board][3] is semi-frequently
updated to reflect a summary of important changes.

[3]: https://trello.com/b/l385BE7a/rebol3-porting-guide-ren-c-branch

Rather than be a brand or product in its own right, this project intends to provide smooth
APIs for embedding an interpreter in C programs...hopefully eventually `rebol.exe` itself.

One of these APIs (libRebol) is "user-friendly" to C programmers, allowing them to avoid the
low-level concerns of the interpreter and just run snippets of code mixed with values, as
easily as:

    int x = 1020;
    Value* negate_function = rebRun("get 'negate", END);

    rebRun("print [", rebInteger(x), "+ (2 *", rebEval(negate_function), "3)]", END);

The other API (libRebolCore) would offer nearly the full range of power that is internally
offered to the core.  It would allow one to pick apart value cells and write extensions
that are equally efficient to built-in natives like REDUCE.  This more heavyweight API
would be used by extensions for which performance is critical.

The current way to explore the new features of Ren-C is using the `r3` console.  It is
*significantly* enhanced from the open-sourced R3-Alpha...with much of its behavior coming
from [userspace Rebol code][4] (as opposed to hardcoded C).  In addition to multi-line
editing and UTF-8 support, it [can be "skinned"][5] and configured in various ways, and
non-C programmers can easily help contribute to enhancing it.

[4]: https://github.com/metaeducation/ren-c/blob/master/src/os/host-console.r
[5]: https://github.com/r3n/reboldocs/wiki/User-and-Console

A C++ binding is also available, and for those interested in a novel application of this
code, they might want to see the experimental console based on it and Qt: [Ren Garden][6].

[6]: http://rencpp.hostilefork.com

In doing this work, the hope is to provide an artifact that would rally common
usage between the [mainline builds][7], community builds, and those made by
[Atronix Engineering][8] and [Saphirion AG][9].

[7]: http://rebolsource.net
[8]: http://www.atronixengineering.com/downloads
[9]: http://development.saphirion.com/rebol/saphir/


## Community

To promote the Rebol community's participation in public forums, development discussion
for Ren-C generally takes place in the [`Rebol*` StackOverflow Chat][10].

[10]: http://rebolsource.net/go/chat-faq

There is [a Discourse forum][11] available for more long-form discussion.

[11]: https://forum.rebol.info

It is also possible to contact the developers through the [Ren-C GitHub Issues][11]
page.  This should be limited to questions regarding the Ren-C builds specifically, as
overall language design wishes and debates are kept in the [`rebol-issues`][12] repository
of Rebol's GitHub.

[12]: https://github.com/metaeducation/ren-c/issues
[13]: https://github.com/rebol/rebol-issues/issues


## Building

As a design goal, compiling Ren-C depends on [very little beyond ANSI C89][14].  Attempts
to reign in compiler dependencies have been a large amount of work, and it still supports
a [number of older platforms][15].  However, if it is compiled with a C++ compiler then
there is significantly more static analysis at build time, to catch errors.

[14]: https://github.com/metaeducation/ren-c/wiki/On-Building-Ren-C-With-Cpp-Compilers
[15]: https://github.com/metaeducation/ren-c/blob/master/make/tools/systems.r

The open-sourced R3-Alpha was based on a build process that depended on GNU make, and
needed an existing R3-Alpha executable in order to generate that makefile (as well as other
generated supporting C files).  This process was recently replaced with a Rebol-only
building solution (`%rebmake.r`) which requires no other tool, and can spawn compilation
processes itself.  Yet it still can generate GNU makefiles or a Visual Studio Solution
if desired.

While this process *works*, it introduced considerable complexity...and needs a relatively
modern Ren-C build to bootstrap--as opposed to a historical R3-Alpha.  For the moment,
some usable binaries are committed into the `%make/` directory for 32/64-bit platforms
on Linux/Mac/Windows.

Building is a matter of picking a config out of the `%make/configs/` directory, or just
taking the defaults.  Options may be overridden on the command line of %make.r (which
is a client of Rebmake):

    r3-make make.r CONFIG=configs/vs2017-x64.r DEBUG=asserts

That would use the Windows r3-make tool to build a project for Visual Studio 2017.

*(Because other priorities have taken the focus away from improvements to this build
process, it would be strongly desirable if community member(s) could get involved to
help streamline and document it!  Since it's now *all* written in Rebol, that should be
more possible.)*


[100]: https://raw.githubusercontent.com/metaeducation/ren-c/master/ren-c-logo.png
[101]: https://travis-ci.org/metaeducation/ren-c.svg?branch=master
