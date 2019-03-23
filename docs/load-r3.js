'use strict'  // <-- FIRST statement! https://stackoverflow.com/q/1335851

// There are two possibilities for how the REPL can accomplish I/O in a way
// that appears synchronous: using pthreads or using the "Emterpreter":
//
// https://emscripten.org/docs/porting/pthreads.html
// https://github.com/kripken/emscripten/wiki/Emterpreter
//
// pthreads rely on SharedArrayBuffer and WASM threading, and hence aren't
// ready in quite all JS environments yet.  However, the resulting build
// products are half the size of what the emterpreter makes, and around
// THIRTY TIMES FASTER.  Hence, the emterpreter is not an approach that is
// likely to stick around any longer than it has to.
//
if (typeof WebAssembly !== "object") {
    throw Error("Your browser doesn't support WebAssembly.")
}

if (typeof Promise !== "function") {
    throw Error("Your browser doesn't support Promise.")
}

var hasShared = typeof SharedArrayBuffer !== "undefined"
console.log("Has SharedArrayBuffer => " + hasShared)

var hasThreads = false
if (hasShared) {
    let test = new WebAssembly.Memory({
        "initial": 0, "maximum": 0, "shared": true
    });
    hasThreads = (test.buffer instanceof SharedArrayBuffer)
}
console.log("Has Threads => " + hasThreads)

var use_emterpreter = ! hasThreads
var os_id = (use_emterpreter ? "0.16.1" : "0.16.2")

console.log("Use Emterpreter => " + use_emterpreter)

var is_localhost = (  // helpful to put certain debug behaviors under this flag
    location.hostname === "localhost"
    || location.hostname === "127.0.0.1"
    //|| location.hostname.startsWith("192.168")
    // disabled, so you can test 'remote' behavior from local net
)
if (is_localhost) {
    var old_alert = window.alert
    window.alert = function(message) {
        console.error(message)
        old_alert(message)
        debugger
    }
}


// THE NAME OF THIS VARIABLE MUST BE SYNCED WITH
// https://metaeducation.s3.amazonaws.com/travis-builds/${OS_ID}/last_git_commit_short.js
// that contains `last_git_commit_short = ${GIT_COMMIT_SHORT}`
// See .travis.yml
//
//var last_git_commit_short = "local" 
//var last_git_commit_short = "remote"
var last_git_commit_short = "mixed" // local for localhost, else remote

// Note these are "promiser" functions, because if they were done as a promise
// it would need to have a .catch() clause attached to it here.  This way, it
// can just use the catch of the promise chain it's put into.)

var load_js_promiser = (url) => new Promise(function(resolve, reject) {
    let script = document.createElement('script')
    script.src = url
    script.onload = () => {resolve(url)}
    script.onerror = () => {reject(url)}
    if (document.body) {
        document.body.appendChild(script)
    } else { document.addEventListener(
        'DOMContentLoaded',
        ()=>{document.body.appendChild(script)}
    )}
})

var last_git_commit_promiser = (os_id) => {
    if (
        last_git_commit_short == "local" ||
        (last_git_commit_short == "mixed"
        && is_localhost)
    ) { // load from ./
        last_git_commit_short = ""
        return Promise.resolve(null)
    } else { // load from amazonaws.com
        return load_js_promiser(
            "https://metaeducation.s3.amazonaws.com/travis-builds/"
            + os_id
            + "/last_git_commit_short.js"
        )
    }
}

var lib_suffixes = [
    ".js", ".wasm",  // all builds
    ".wast", ".temp.asm.js",  // debug only
    ".bytecode",  // emterpreter builds only
    ".js.mem", ".worker.js"  // non-emterpreter builds only
]


// At this moment, there are 3 files involved in the download of the library:
// a .JS loader stub, a .WASM loader stub, and a large emterpreter .BYTECODE
// file.  See notes on the hopefully temporary use of the "Emterpreter",
// without which one assumes only a .wasm file would be needed.
//
// If you see files being downloaded multiple times in the Network tab of your
// browser's developer tools, this is likely because your webserver is not
// configured correctly to offer the right MIME type for the .wasm file...so
// it has to be interpreted by JavaScript.  See the README.md for how to
// configure your server correctly.
//
function libRebolComponentURL(suffix) {  // suffix includes the dot
    if (!lib_suffixes.includes(suffix))
        throw Error("Unknown libRebol component extension: " + suffix)

    if (use_emterpreter) {
        if (suffix == ".worker.js")
            throw Error(
                "Asking for " + suffix + " file "
                + " in an emterpreter build (should only be for pthreads)"
            )
    }
    else {
        if (suffix == ".bytecode")
            throw Error(
                "Asking for " + suffix + " file "
                + " in an emterpreter build (should only be for pthreads)"
            )
    }

    // Due to origin policy restrictions, you have to have the libr3.worker.js
    // in the same place your page is coming from.  Fortunately this is a
    // fixed file.
    //
    if (suffix == ".worker.js")
        return "libr3" + suffix

    // !!! These files should only be generated if you are debugging, and
    // are optional.  But it seems locateFile() can be called to ask for
    // them anyway--even if it doesn't try to fetch them (e.g. no entry in
    // the network tab that tried and failed).  Review build settings to
    // see if there's a way to formalize this better to know what's up.
    //
    if (false) {
        if (suffix == ".wast" || suffix == ".temp.asm.js")
            throw Error(
                "Asking for " + suffix + " file "
                + " in a non-debug build (only for debug builds)")
    }
    let dir = (
        last_git_commit_short  // empty string ("") is falsey in JS
        ? "https://metaeducation.s3.amazonaws.com/travis-builds/"
        : "./"
    ) + os_id

    let opt_dash = last_git_commit_short ? "-" : "";

    return dir + "/libr3" + opt_dash + last_git_commit_short + suffix
}


var Module = {
    //
    // For errors like:
    //
    //    "table import 1 has a larger maximum size 37c than the module's
    //     declared maximum 890"
    //
    // The total memory must be bumped up.  These large sizes occur in debug
    // builds with lots of assertions and symbol tables.  Note that the size
    // may appear smaller than the maximum in the error message, as previous
    // tables (e.g. table import 0 in the case above) can consume memory.
    //
    // !!! Messing with this setting never seemed to help.  See the emcc
    // parameter ALLOW_MEMORY_GROWTH for another possibility.
    //
 /* TOTAL_MEMORY: 16 * 1024 * 1024, */

    locateFile: function(s) {
        //
        // function for finding %libr3.wasm  (Note: memoryInitializerPrefixURL
        // for bytecode was deprecated)
        //
        // https://stackoverflow.com/q/46332699
        //
        console.info("Module.locateFile() asking for .wasm address of " + s)

        let stem = s.substr(0, s.indexOf('.'))
        let suffix = s.substr(s.indexOf('.'))

        // Although we rename the files to add the Git Commit Hash before
        // uploading them to S3, it seems that for some reason the .js hard
        // codes the name the file was built under in this request.  :-/
        // So even if the request was for `libr3-xxxxx.js` it will be asked
        // in this routine as "Where is `libr3.wasm`
        //
        // For the moment, sanity check to libr3.  But it should be `rebol`,
        // or any name you choose to build with.
        //
        if (stem != "libr3")
            throw Error("Unknown libRebol stem: " + stem)

        return libRebolComponentURL(suffix)
    },

    // This is a callback that happens sometime after you load the emscripten
    // library (%libr3.js in this case).  It's turned into a promise instead
    // of a callback.  Sanity check it's not used prior by making it a string.
    //
    onRuntimeInitialized: "<mutated from a callback into a Promise>",

    // If you use the emterpreter, it balloons up the size of the javascript
    // unless you break the emterpreter bytecode out into a separate file.
    // You have to get the data into the Module['emterpreterFile'] before
    // trying to load the emscripten'd code.
    //
    emterpreterFile: "<if `use_emterpreter`, fetch() of %libr3.bytecode>"

    // The rest of these fields will be filled in by the boilerplate of the
    // Emterpreter.js file when %libr3.js loads (it looks for an existing
    // Module and adds to it, but this is also how you parameterize options.)
}


//=// CONVERTING CALLBACKS TO PROMISES /////////////////////////////////////=//
//
// https://stackoverflow.com/a/22519785
//

var dom_content_loaded_promise = new Promise(function(resolve, reject) {
    document.addEventListener('DOMContentLoaded', resolve)
})

var onGuiInitialized
var gui_init_promise = new Promise(function(resolve, reject) {
    //
    // The GUI has to be initialized (DOM initialization, etc.) before we can
    // even use HTML to show status text like "Running Mezzanine", etc.  When
    // all the GUI's services are available it will call onGuiInitialized().
    // This converts that into a promise so it can be used in a clearer-to-read
    // linear .then() sequence.
    //
    onGuiInitialized = resolve
})

var runtime_init_promise = new Promise(function(resolve, reject) {
    //
    // The load of %libr3.js will at some point will trigger a call to
    // onRuntimeInitialized().  We set it up so that when it does, it will
    // resolve this promise (used to trigger a .then() step).
    //
    Module.onRuntimeInitialized = resolve
})


// If we are using the emterpreter, Module.emterpreterFile must be assigned
// before the %libr3.js starts running.  And it will start running some time
// after the dynamic `<script>` is loaded.
//
// See notes on short_hash_promiser for why this is a "promiser", not a promise
//
var bytecode_promiser
if (!use_emterpreter)
    bytecode_promiser = () => {
        console.log("Not emterpreted libr3.js, not requesting bytecode")
        return Promise.resolve()
    }
else {
    bytecode_promiser = () => {
        let url = libRebolComponentURL(".bytecode")
        console.log("Emterpreted libr3.js, requesting bytecode from:" + url)

        return fetch(url)
          .then(function(response) {

            // https://www.tjvantoll.com/2015/09/13/fetch-and-errors/
            if (!response.ok)
                throw Error(response.statusText)  // handled by .catch() below

            return response.arrayBuffer()  // arrayBuffer() method is a promise

          }).then(function(buffer) {

            Module.emterpreterFile = buffer  // must load before emterpret()-ing
          })
    }
}


// Initialization is written as a series of promises for, uh, "simplicity".
//
// !!! Review use of Promise.all() for steps which could be run in parallel.
//
var r3_ready_promise = last_git_commit_promiser(os_id) // set last_git_commit_short
  .then(bytecode_promiser)  // needs last_git_commit_short
  .then(function() {
      load_js_promiser(libRebolComponentURL(".js"))
  })
  .then(function() {
    // ^-- The above will eventually trigger runtime_init_promise, but don't
    // wait on that just yet.  Instead just get the loading process started,
    // then wait on the GUI (which 99.9% of the time should finish first) so we
    // can display a "loading %libr3.js" message in the browser window.
    //
    return gui_init_promise

  }).then(function() {  // our onGuiInitialized() message currently has no args
    let msg = 'Loading/Running ' + libRebolComponentURL(".js") + '...'
    if (use_emterpreter) {
        msg +=("\nUsing Emterpreter is SLOW! Be patient...")
    }
    console.log(msg)

    return runtime_init_promise

  }).then(function() {  // emscripten's onRuntimeInitialized() has no args

    console.log('Executing Rebol boot code...')
    reb.Startup()

    // There is currently no method to dynamically load extensions with
    // r3.js, so the only extensions you can load are those that are picked
    // to be built-in while compiling the lib.  The "JavaScript extension" is
    // essential--it contains JS-NATIVE and JS-AWAITER.
    //
    console.log('Initializing extensions')
    reb.Elide(
        "for-each collation builtin-extensions",
            "[load-extension collation]"
    )
  })
