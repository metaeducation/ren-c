## Rebmake Configuration Files For Various Builds

The quirky and Byzantine Rebmake system actually had a fairly simple idea
of how builds would be configured.  All configurations inherited from a
%default-config.r, which defined a number of fields that they would then
override with specifics.

For example: the default config defines `os-id: null` to say that every config
needs to supply that OS-ID.  Or `optimize: 2` to say that if there's no
overriding it, the optimization level will be set to 2.

You name the specific config you want to use as a parameter to the Rebol
%make.r, and it may override these settings.  But you can also override them
on the command line:

    r3 make.r config: configs/emscripten.r target: makefile optimize: 0

So %emscripten.r inherits `optimize: 2` from %default-config.r, and then
maybe it sets it to `optimize: s`, but you could then further override that
here with `optimize: 0`.

That's the basic gist of what the concept was.  But another premise was that
these configurations could run Rebol code--locating tools on your platform
using CALL, or looking in environment variables.

## Writing Code In A Config Specific to the OS You're Building *On*

The platform you are building *for* is determined by the config itself.  Once
you've specified that, then things like %make-spec.r files for extensions
can react to things in `platform-config`, e.g. `platform-config.os-base`

If you want to write code specific to the platform you are building *on* in
the config (such as to know how to find toolchains on that particular platform)
there's not currently a better way to do it than decoding the numbers in the
`system.version` field (e.g. if `system.version.4` is 3, it's Windows).  See
the %platforms.r file for this kludgey-yet-longstanding list.

## Unfinished Idea: Per-Extension Extensible Config Parameters

@szeng tried something where in the %make-spec.r file you could have "options".
The only example was this:

    options: [
        odbc-requires-ltdl [logic?] ()
    ]

I don't know how it was supposed to work, but the idea of an extension being
able to publish its options seems like a good one.  Perhaps that should be
in the header of the %make-spec.r instead of assigned in the body.

In any case...Rebmake has been hacked along over many years, to build the
system by hook or by crook.  Hand-maintaining makefiles or CMake files would
have been far easier, but provide less day-to-day education about the reality
of evolving the language!
