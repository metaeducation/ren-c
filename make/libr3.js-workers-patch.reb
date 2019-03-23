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
           throw Error(response.statusText)
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

if not parse text [
  to find1 remove find1 insert patch1
  thru find2 insert patch2
  to end
] [print "failed" quit/with 255]

write file text
