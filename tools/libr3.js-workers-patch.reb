; Quick and dirty hack that post-processes a generated libr3.js in order to
; get it to fetch() a Blob from a CORS-enabled server to create a worker,
; since the way the emscripten pthread emulation does it (`new Worker(url)`)
; won't work cross-domain.
;
; https://github.com/emscripten-core/emscripten/issues/8338

find1: "var worker = new Worker(pthreadMainJs);"
patch1: {
   //<ren-c-modification>
   //
   // This used to just use:
   //
   //     var worker = new Worker(pthreadMainJs);
   //
   // but Web Workers will not (in browsers at time of writing) load from a
   // remote host, even if CORS is enabled.  You get the error:
   //
   // > Uncaught (in promise) DOMException: Failed to construct 'Worker':
   // > Script at 'http://example.com/my-lib.worker.js' cannot be accessed from
   // > origin 'http://example.com'.
   //
   // Hence we use fetch() to get a Blob and then do `new Worker(blob)`:
   //
   // https://stackoverflow.com/q/25458104/
   //
   console.log(
     "Requesting " + pthreadMainJs + " as blob vs. new Worker(url)."
     + " See https://stackoverflow.com/q/25458104/ for why we have to."
   )
   fetch(pthreadMainJs)
     .then(function(response) {
       // https://www.tjvantoll.com/2015/09/13/fetch-and-errors/
       if (!response.ok)
           err(response.statusText)
       return response.blob()
     })
     .then(function(blob) ^{
       console.log("Retrieved worker blob, calling `new Worker(blob)`")
       var worker = new Worker(URL.createObjectURL(blob))
       console.log("Blob-based worker creation successful!")
       ;  // semicolon is necessary due to ensuing `(function`
       // https://stackoverflow.com/q/31013221/
   //
   //</ren-c-modification>
}

find2: "PThread.unusedWorkerPool.push(worker);"
patch2:{
   //<ren-c-modification>
   ^})
   //</ren-c-modification>
}

file: to-file system/options/args/1
text: read/string file

parse text [
  to find1 remove find1
  insert patch1

  ; There are two occurrences of "PThread.unusedWorkerPool.push(worker);" at
  ; time of writing.  We don't want the first one, skip until after the
  ; `PthreadWorkerInit: PthreadWorkerInit`
  ;
  thru "PthreadWorkerInit: PthreadWorkerInit"

  thru find2
  insert patch2

  to end
] else [
    fail [
        "Could not perform patch for"
        https://github.com/emscripten-core/emscripten/issues/8338
    ]
]

write file text
