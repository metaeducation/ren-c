//
//  File: %mod-view.c
//  Summary: "Beginnings of GUI Interface as an extension"
//  Section: Extension
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 Atronix Engineering
// Copyright 2012-2017 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//=////////////////////////////////////////////////////////////////////////=//
//
// !!! Currently these are two file pickers that interact with Windows or
// GTK to allow choosing files or folders.  Originally the feature was found
// in Atronix R3, through the "hostkit" and COMMAND! extension mechanism.
// It's not clear why the file and directory picker codebases are separate,
// since the common dialogs seem able to do either.
//
// For something of this relatively simple nature, it would be ideal if the
// code did not know about REBSER* or other aspects of the internal API.
// But the external API is not quite polished yet, so some fledgling features
// are being used here.
//

#ifdef TO_WINDOWS
    #include <windows.h>
    #include <process.h>
    #include <shlobj.h>

    #ifdef IS_ERROR
        #undef IS_ERROR //winerror.h defines, Rebol has a different meaning
    #endif
#else
    #if !defined(__cplusplus) && defined(TO_LINUX)
        //
        // See feature_test_macros(7), this definition is redundant under C++
        //
        #define _GNU_SOURCE // Needed for pipe2 when #including <unistd.h>
    #endif
    #include <unistd.h>

    #include <errno.h>

    // !!! Rebol historically has been monolithic, and extensions could not be
    // loaded as DLLs.  This meant that linking to a dependency like GTK could
    // render the executable useless if that library was unavailable.  So
    // there was a fairly laborious loading of dozens of individual GTK
    // functions with #include <dlfcn.h> and dlsym() vs just calling them
    // directly e.g.
    //
    //    void (*gtk_file_chooser_set_current_folder) (
    //        GtkFileChooser *chooser,
    //        const gchar *name
    //    ) = dlsym(libgtk, "gtk_file_chooser_set_current_folder");
    //
    // (See %/src/os/linux/file-chooser-gtk.c in Atronix R3)
    //
    // But even Rebol2 had a distinct /View and /Core build, so the View would
    // presume availability of whatever library (e.g. GTK) and not run if you
    // did not have it.  But if that is a problem, there's now another option,
    // which is to make the extension a DLL that you optionally load.
    //
    // If a truly loosely-bound GTK is needed, that problem should be solved
    // at a more general level so the code doesn't contain so much manually
    // entered busywork.  This presumes you link the extension to GTK (or the
    // whole executable to GTK if you are building the extension into it)
    //
    #if defined(USE_GTK_FILECHOOSER)
        #include <gtk/gtk.h>
    #endif
#endif

#include "sys-core.h"
#include "sys-ext.h"

#include "tmp-mod-view-first.h"


// !!! This was around saying it was "used to detect modal non-OS dialogs".
// The usage was in the Rebol_Window_Proc() in Atronix's R3 code.
//
REBOOL osDialogOpen = FALSE;

#define MAX_FILE_REQ_BUF (16*1024)


//
//  request-file*: native/export [
//
//  {Asks user to select file(s) and returns full file path(s)}
//
//      return: [file! block! blank!]
//          {Blank if canceled, otherwise a path or block of paths}
//      /save
//          "File save mode"
//      /multi
//          {Allows multiple file selection, returned as a block}
//      /file
//      name [file!]
//          "Default file name or directory"
//      /title
//      text [string!]
//          "Window title"
//      /filter
//      list [block!]
//          "Block of filters (filter-name filter)"
//  ]
//
REBNATIVE(request_file_p)
{
    INCLUDE_PARAMS_OF_REQUEST_FILE_P;

    // Files to return will be collected and returned on the stack
    //
    REBDSP dsp_orig = DSP;

    REBCTX *error = NULL;

    osDialogOpen = TRUE;

#ifdef TO_WINDOWS
    OPENFILENAME ofn;
    memset(&ofn, '\0', sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);

    ofn.hwndOwner = NULL; // !!! Should be set to something for modality
    ofn.hInstance = NULL; // !!! Also should be set for context (app type)

    wchar_t *lpstrFilter;
    if (REF(filter)) {
        DECLARE_MOLD (mo);
        Push_Mold(mo);

        RELVAL *item;
        for (item = VAL_ARRAY_AT(ARG(list)); NOT_END(item); ++item) {
            Form_Value(mo, item);
            Append_Utf8_Codepoint(mo->series, '\0');
        }
        Append_Utf8_Codepoint(mo->series, '\0');

        REBSER *ser = Pop_Molded_String(mo);

        // !!! We don't really want to be exposing REBSERs to this level of
        // interface code.  In trying to coax it toward REBVAL-oriented APIs
        // pretend we built the string as a value (perhaps best as a BINARY!
        // produced by helper Rebol code).  Note that the series is managed
        // once it goes through the Init_String, so it can't be freed.
        //
        DECLARE_LOCAL (hack);
        Init_String(hack, ser);
        lpstrFilter = rebSpellingOfAllocW(NULL, hack);
    }
    else {
        // Currently the implementation of default filters is in usermode,
        // done by a HIJACK of REQUEST-FILE with an adaptation that tests
        // if no filters are given and supplies a block.
        //
        lpstrFilter = NULL;
    }
    ofn.lpstrFilter = lpstrFilter;

    ofn.lpstrCustomFilter = NULL; // would let user save filters they add
    ofn.nMaxCustFilter = 0;

    // Currently the first filter provided is chosen, though it would be
    // possible to highlight one of them (maybe put it in a GROUP!?)
    //
    ofn.nFilterIndex = 0;

    wchar_t* lpstrFile = OS_ALLOC_N(wchar_t, MAX_FILE_REQ_BUF);
    ofn.lpstrFile = lpstrFile;
    ofn.lpstrFile[0] = '\0'; // may be filled with ARG(name) below
    ofn.nMaxFile = MAX_FILE_REQ_BUF - 1; // size in characters, space for NULL

    ofn.lpstrFileTitle = NULL; // can be used to get file w/o path info...
    ofn.nMaxFileTitle = 0; // ...but we want the full path

    wchar_t *lpstrInitialDir;
    if (REF(file)) {
        REBCNT path_len;
        const REBOOL full = TRUE;
        wchar_t *path = rebFileToLocalAllocW(&path_len, ARG(name), full);

        // If the last character doesn't indicate a directory, that means
        // we are trying to pre-select a file, which we do by copying the
        // content into the ofn.lpstrFile field.
        //
        if (path[path_len - 1] != '\\') {
            REBCNT n;
            if (path_len + 2 > ofn.nMaxFile)
                n = ofn.nMaxFile - 2;
            else
                n = path_len;
            wcsncpy(ofn.lpstrFile, path, n);
            lpstrFile[n] = '\0';
            lpstrInitialDir = NULL;
            OS_FREE(path);
        }
        else {
            // Otherwise it's a directory, and we have to put that in the
            // lpstrInitialDir (ostensibly because of some invariant about
            // lpstrFile that it can't hold a directory when your goal is
            // to select a file?
            //
            lpstrInitialDir = path;
        }
    }
    else
        lpstrInitialDir = NULL;
    ofn.lpstrInitialDir = lpstrInitialDir;

    wchar_t *lpstrTitle;
    if (REF(title))
        lpstrTitle = rebSpellingOfAllocW(NULL, ARG(text));
    else
        lpstrTitle = NULL; // Will use "Save As" or "Open" defaults
    ofn.lpstrTitle = lpstrTitle;

    // !!! What about OFN_NONETWORKBUTTON?
    ofn.Flags = OFN_HIDEREADONLY | OFN_EXPLORER | OFN_NOCHANGEDIR;
    if (REF(multi))
        ofn.Flags |= OFN_ALLOWMULTISELECT;

    // These can be used to find the offset in characters from the beginning
    // of the lpstrFile to the "File Title" (name plus extension, sans path)
    // and the extension (what follows the dot)
    //
    ofn.nFileOffset = 0;
    ofn.nFileExtension = 0;

    // Currently unused stuff.
    //
    ofn.lpstrDefExt = NULL;
    ofn.lCustData = cast(LPARAM, NULL);
    ofn.lpfnHook = NULL;
    ofn.lpTemplateName = NULL;

    BOOL ret;
    if (REF(save))
        ret = GetSaveFileName(&ofn);
    else
        ret = GetOpenFileName(&ofn);

    if (NOT(ret)) {
        DWORD cderr = CommDlgExtendedError();
        if (cderr == 0) {
            //
            // returned FALSE because of cancellation, that's fine, just
            // don't push anything to the data stack and we'll return blank
        }
        else if (cderr == FNERR_BUFFERTOOSMALL) // ofn.nMaxFile too small
            error = Error_User("dialog buffer too small for selection");
        else
            error = Error_User("common dialog failure CDERR_XXX");
    }
    else {
        if (NOT(REF(multi))) {
            const REBOOL is_dir = FALSE;
            REBVAL *solo = rebLocalToFileW(ofn.lpstrFile, is_dir);
            DS_PUSH_TRASH;
            Move_Value(DS_TOP, solo);
            rebRelease(solo);
        }
        else {
            const wchar_t *item = ofn.lpstrFile;

            REBCNT item_len = wcslen(item);
            assert(item_len != 0); // must have at least one item for success
            if (wcslen(item + item_len + 1) == 0) {
                //
                // When there's only one item in a multi-selection scenario,
                // that item is the filename including path...the lone result.
                //
                const REBOOL is_dir = FALSE;
                REBVAL *solo = rebLocalToFileW(item, is_dir);
                DS_PUSH_TRASH;
                Move_Value(DS_TOP, solo);
                rebRelease(solo);
            }
            else {
                // More than one item means the first is a directory, and the
                // rest are files in that directory.  We want to merge them
                // together to make fully specified paths.
                //
                const REBOOL is_dir = TRUE;
                REBVAL *dir = rebLocalToFileW(item, is_dir);
                
                item += item_len + 1; // next

                REBCNT dir_len;
                const REBOOL full = TRUE;
                wchar_t *dir_wide = rebFileToLocalAllocW(&dir_len, dir, full);

                while ((item_len = wcslen(item)) != 0) {
                    wchar_t *buffer = OS_ALLOC_N(
                        wchar_t, dir_len + item_len + 1 // null terminator
                    );

                    wcscpy(buffer, dir_wide);
                    wcscat(buffer, item);

                    const REBOOL is_dir = FALSE;
                    REBVAL *file = rebLocalToFileW(buffer, is_dir);
                    OS_FREE(buffer);

                    DS_PUSH_TRASH;
                    Move_Value(DS_TOP, file);

                    item += item_len + 1; // next
                }

                OS_FREE(dir_wide);
                rebRelease(dir);
            }
        }
    }

    // Being somewhat paranoid that Windows won't corrupt the pointers in
    // the OPENFILENAME structure...so we free caches of what we put in.
    //
    if (REF(filter))
        OS_FREE(lpstrFilter);
    OS_FREE(lpstrFile);
    if (REF(file) && lpstrInitialDir != NULL)
        OS_FREE(lpstrInitialDir);
    if (REF(title))
        OS_FREE(lpstrTitle);

#elif defined(USE_GTK_FILECHOOSER)

    // gtk_init_check() will not terminate the program if gtk cannot be
    // initialized, and it will return TRUE if GTK is successfully initialized
    // for the first time or if it's already initialized.
    //
    int argc = 0;
    if (NOT(gtk_init_check(&argc, NULL)))
        fail ("gtk_init_check() failed");
    
    if (REF(filter)) {
        // !!! wasn't implemented in GTK for Atronix R3
        //
        UNUSED(ARG(list));
    }

    char *title;
    if (REF(title))
        title = rebSpellingOfAlloc(NULL, ARG(text));
    else
        title = NULL;

    // !!! Using a NULL parent causes console to output:
    // "GtkDialog mapped without a transient parent. This is discouraged."
    //
    GtkWindow *parent = NULL;

    GtkWidget *dialog = gtk_file_chooser_dialog_new(
        title == NULL
            ? (REF(save) ? "Save file" : "Open File")
            : title,
        parent,
        REF(save)
            ? GTK_FILE_CHOOSER_ACTION_SAVE
            : GTK_FILE_CHOOSER_ACTION_OPEN, // or SELECT_FOLDER, CREATE_FOLDER

        // First button and button response (underscore indicates hotkey)
        "_Cancel",
        GTK_RESPONSE_CANCEL,

        // Second button and button response
        REF(save) ? "_Save" : "_Open",
        GTK_RESPONSE_ACCEPT,

        cast(const char*, NULL) // signal no more buttons
    );

    GtkFileChooser *chooser = GTK_FILE_CHOOSER(dialog);

    gtk_file_chooser_set_select_multiple(chooser, REF(multi));

    REBYTE *name;
    if (REF(file)) {
        name = rebSpellingOfAlloc(NULL, ARG(name));
        gtk_file_chooser_set_current_folder(chooser, cast(gchar*, name));
    }
    else
        name = NULL;

    if (gtk_dialog_run(GTK_DIALOG(dialog)) != GTK_RESPONSE_ACCEPT) {
        //
        // If there was a cancellation, don't push any FILE!s to the stack.
        // A blank will be returned later.
    }
    else {
        // On success there are two different code paths, because the multi
        // file return convention (a singly linked list of strings) is not the
        // same as the single file return convention (one string).
        
        if (REF(multi)) {
            REBYTE *folder = b_cast(
                gtk_file_chooser_get_current_folder(chooser)
            );

            if (folder == NULL)
                error = Error_User("folder can't be represented locally");
            else {
                GSList *list = gtk_file_chooser_get_filenames(chooser);
                GSList *item;
                for (item = list; item != NULL; item = item->next) {
                    //
                    // !!! The directory seems to already be included...though
                    // there was code here that tried to add it (?)  If it
                    // becomes relevant, `folder` is available to prepend.
                    //
                    REBVAL *file = rebFile(item->data); // UTF-8 data
                    DS_PUSH_TRASH;
                    Move_Value(DS_TOP, file);
                    rebRelease(file);
                }
                g_slist_free(list);

                g_free(folder);
            }
        }
        else {
            // filename is in UTF-8, directory seems to be included.
            //
            REBVAL *file = rebFile(gtk_file_chooser_get_filename(chooser));
            DS_PUSH_TRASH;
            Move_Value(DS_TOP, file);
            g_free(filename);
        }
    }

    gtk_widget_destroy(dialog);

    if (REF(file))
        OS_FREE(name);
    if (REF(title))
        OS_FREE(title);

    while (gtk_events_pending()) {
        //
        // !!! Commented out code here invoked gtk_main_iteration_do(0),
        // to whom it may concern who might be interested in any of this.
        //
        gtk_main_iteration ();
    }

#else
    UNUSED(REF(save));
    UNUSED(REF(multi));
    UNUSED(REF(file));
    UNUSED(ARG(name));
    UNUSED(REF(title));
    UNUSED(ARG(text));
    UNUSED(REF(filter));
    UNUSED(ARG(list));

    error = Error_User("REQUEST-FILE only on GTK and Windows at this time");
#endif

    osDialogOpen = FALSE;

    // The error is broken out this way so that any allocated strings can
    // be freed before the failure.
    //
    if (error != NULL)
        fail (error);

    if (DSP == dsp_orig)
        return R_BLANK;

    if (REF(multi)) {
        //
        // For the caller's convenience, return a BLOCK! if they requested
        // /MULTI and there's even just one file.  (An empty block might even
        // be better than BLANK! for that case?)
        //
        Init_Block(D_OUT, Pop_Stack_Values(dsp_orig));
        return R_OUT;
    }

    assert(IS_FILE(DS_TOP));
    Move_Value(D_OUT, DS_TOP);

    assert(DSP == dsp_orig + 1); // should be only one pushed, so check...
    DS_DROP_TO(dsp_orig); // ...but use DS_DROP_TO just to be safe in release

    return R_OUT;
}


#ifdef TO_WINDOWS
int CALLBACK ReqDirCallbackProc(
    HWND hWnd,
    UINT uMsg,
    LPARAM lParam,
    LPARAM lpData // counterintuitively, this is provided from bi.lParam
){
    UNUSED(lParam);

    const wchar_t* dir = cast(wchar_t*, lpData);

    static REBOOL inited = FALSE; 
    switch (uMsg) {
    case BFFM_INITIALIZED:
        if (dir != NULL)
            SendMessage(hWnd, BFFM_SETSELECTION, TRUE, cast(LPARAM, dir));
        SetForegroundWindow(hWnd);
        inited = TRUE;
        break;

    case BFFM_SELCHANGED:
        if (inited && dir != NULL) {
            SendMessage(hWnd, BFFM_SETSELECTION, TRUE, cast(LPARAM, dir));
            inited = FALSE;
        }
        break;
    }
    return 0;
}
#endif


//
//  request-dir*: native/export [
//
//  "Asks user to select a directory and returns it as file path"
//
//      /title
//          "Custom dialog title text"
//      text [string!]
//      /path
//          "Default directory path"
//      dir [file!]
//  ]
//
REBNATIVE(request_dir_p)
//
// !!! This came from Saphirion/Atronix R3-View.  It said "WARNING: TEMPORARY
// implementation! Used only by host-core.c Will be most probably changed
// in future."  It was only implemented for Windows, and has a dependency
// on some esoteric shell APIs which requires linking to OLE32.
//
// The code that was there has been resurrected well enough to run, but is
// currently disabled to avoid the OLE32 dependency.
{
    INCLUDE_PARAMS_OF_REQUEST_DIR_P;

    REBCTX *error = NULL;

#if defined(USE_WINDOWS_DIRCHOOSER)
    //
    // COM must be initialized to use SHBrowseForFolder.  BIF_NEWDIALOGSTYLE
    // is incompatible with COINIT_MULTITHREADED, the dialog will hang and
    // do nothing.
    //
    HRESULT hresult = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (hresult == S_OK) {
        // Worked fine
    }
    else if (hresult == S_FALSE) {
        // Already initialized on this thread
    }
    else {
        fail ("Failure during CoInitializeEx()");
    }

    BROWSEINFO bi;
    bi.hwndOwner = NULL;
    bi.pidlRoot = NULL;

    wchar_t display[MAX_PATH];
    display[0] = '\0';
    bi.pszDisplayName = display; // assumed length is MAX_PATH

    if (REF(title))
        bi.lpszTitle = rebSpellingOfAllocW(NULL, ARG(text));
    else
        bi.lpszTitle = L"Please, select a directory...";

    // !!! Using BIF_NEWDIALOGSTYLE is a much nicer dialog, but it appears to
    // be incompatible with BIF_RETURNONLYFSDIRS.  Internet reports confirm
    // inconsistent behavior (seen on Windows 10) and people having to
    // manually implement the return-only-directory feature in the dialog
    // callback.
    //
    bi.ulFlags = BIF_EDITBOX
        | BIF_RETURNONLYFSDIRS
        | BIF_SHAREABLE;

    // If you pass in a directory, there is a callback registered that will
    // set that directory as the default when it comes up.  (Although the
    // field is called `bi.lParam`, it gets passed as the `lpData`)
    //
    bi.lpfn = ReqDirCallbackProc;
    if (REF(path))
        bi.lParam = cast(LPARAM, rebSpellingOfAllocW(NULL, ARG(dir)));
    else
        bi.lParam = cast(LPARAM, NULL);

    osDialogOpen = TRUE;
    LPCITEMIDLIST pFolder = SHBrowseForFolder(&bi);
    osDialogOpen = FALSE;

    wchar_t folder[MAX_PATH];
    if (pFolder == NULL)
        Init_Blank(D_OUT);
    else if (NOT(SHGetPathFromIDList(pFolder, folder)))
        error = Error_User("SHGetPathFromIDList failed");
    else {
        REBVAL *file = rebFile(folder);
        Move_Value(D_OUT, file);
        rebRelease(file);
    }

    if (REF(title))
        OS_FREE(cast(wchar_t*, bi.lpszTitle));
    if (REF(path))
        OS_FREE(cast(wchar_t*, bi.lParam));
#else
    UNUSED(REF(title));
    UNUSED(ARG(text));
    UNUSED(REF(path));
    UNUSED(ARG(dir));

    error = Error_User("Temporary implementation of REQ-DIR only on Windows");
#endif

    if (error != NULL)
        fail (error);

    return R_OUT;
}


#include "tmp-mod-view-last.h"
