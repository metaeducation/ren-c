//
//  File: %mod-view.c
//  Summary: "Beginnings of GUI Interface as an extension"
//  Section: Extension
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2020 Ren-C Open Source Contributors
// Copyright 2012 Atronix Engineering
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//
// !!! Currently these are two file pickers that interact with Windows or
// GTK to allow choosing files or folders.  Originally the feature was found
// in Atronix R3, through the "hostkit" and COMMAND! extension mechanism.
// It's not clear why the file and directory picker codebases are separate,
// since the common dialogs seem able to do either.
//

#include "reb-config.h"

#if TO_WINDOWS
    // `#define WIN32_LEAN_AND_MEAN` seems to omit FNERR_BUFFERTOOSMALL :-/
    #include <windows.h>

    #include <commdlg.h>

    #include <process.h>
    #include <shlobj.h>
#else
    #if !defined(__cplusplus) && TO_LINUX
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

#include "assert-fix.h"
#include "c-enhanced.h"

#include "tmp-mod-view.h"
typedef RebolValue Value;


#define MAX_FILE_REQ_BUF (16*1024)


//
//  export /request-file*: native [
//
//  "Asks user to select file(s) and returns full file path(s)"
//
//      return: "Null if canceled, otherwise a path or block of paths"
//          [~null~ file! block!]
//      :save "File save mode"
//      :multi "Allows multiple file selection, returned as a block"
//      :initial "Default file name or directory"
//          [file!]
//      :title "Window title"
//          [text!]
//      :filter "Block of filters (filter-name filter)"
//          [block!]
//  ]
//
DECLARE_NATIVE(request_file_p)
{
    INCLUDE_PARAMS_OF_REQUEST_FILE_P;

    Value* results = rebValue("copy []");  // collected in block and returned

    Value* error = nullptr;  // error saved to raise after buffers freed

    bool saving = rebDid("save");
    bool multi = rebDid("multi");

  #if TO_WINDOWS
    OPENFILENAME ofn;
    memset(&ofn, '\0', sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);

    ofn.hwndOwner = nullptr;  // !!! Should be set to something for modality
    ofn.hInstance = nullptr;  // !!! Also should be set for context (app type)

    WCHAR *filter_utf16;
    if (rebDid("filter")) {
        //
        // The technique used is to separate the filters by '\0', and end
        // with a doubled up `\0\0`.  Ren-C strings don't allow embedded `\0`
        // bytes, and wide character strings can't be easily built in binaries.
        // Do the delimiting with tab characters, then do a pass to replace
        // replace them in the extracted wide character buffer.
        //
        rebElide(
            "for-each 'item filter [",
                "if find item tab [fail -{TAB chars not legal in filters}-]",
            "]"
        );
        filter_utf16 = rebSpellWide("delimit:tail tab filter");
        WCHAR* pwc;
        for (pwc = filter_utf16; *pwc != 0; ++pwc) {
            if (*pwc == '\t')
                *pwc = '\0';
        }
    }
    else {
        // Currently the implementation of default filters is in usermode,
        // done by a HIJACK of REQUEST-FILE with an adaptation that tests
        // if no filters are given and supplies a block.
        //
        filter_utf16 = nullptr;
    }
    ofn.lpstrFilter = filter_utf16;

    ofn.lpstrCustomFilter = nullptr; // would let user save filters they add
    ofn.nMaxCustFilter = 0;

    // Currently the first filter provided is chosen, though it would be
    // possible to highlight one of them (maybe put it in a GROUP!?)
    //
    ofn.nFilterIndex = 0;

    WCHAR* chosen_utf16 = rebAllocN(WCHAR, MAX_FILE_REQ_BUF);
    ofn.lpstrFile = chosen_utf16;
    ofn.lpstrFile[0] = '\0';  // may be filled with `name` argument below
    ofn.nMaxFile = MAX_FILE_REQ_BUF - 1;  // size in characters, space for \0

    ofn.lpstrFileTitle = nullptr;  // can be used to get file w/o path info...
    ofn.nMaxFileTitle = 0;  // ...but we want the full path

    WCHAR *initial_dir_utf16;
    if (rebNot("empty? maybe initial")) {
        WCHAR* initial_utf16 = rebSpellWide("file-to-local:full initial");
        size_t initial_len = wcslen(initial_utf16);

        // If the last character doesn't indicate a directory, that means
        // we are trying to pre-select a file, which we do by copying the
        // content into the ofn.lpstrFile field.
        //
        if (initial_utf16[initial_len - 1] != '\\') {
            size_t len;
            if (initial_len + 2 > ofn.nMaxFile)
                len = ofn.nMaxFile - 2;
            else
                len = initial_len;
            wcsncpy_s(chosen_utf16, MAX_FILE_REQ_BUF, initial_utf16, len);
            chosen_utf16[len] = '\0';
            rebFree(initial_utf16);

            initial_dir_utf16 = nullptr;
        }
        else {
            // Otherwise it's a directory, and we have to put that in the
            // lpstrInitialDir (ostensibly because of some invariant about
            // lpstrFile that it can't hold a directory when your goal is
            // to select a file?
            //
            initial_dir_utf16 = initial_utf16;
        }
    }
    else
        initial_dir_utf16 = nullptr;
    ofn.lpstrInitialDir = initial_dir_utf16;

    WCHAR *title_utf16 = rebSpellWideMaybe("title");
    ofn.lpstrTitle = title_utf16;  // nullptr defaults to "Save As" or "Open"

    // !!! What about OFN_NONETWORKBUTTON?
    ofn.Flags = OFN_HIDEREADONLY | OFN_EXPLORER | OFN_NOCHANGEDIR;
    if (multi)
        ofn.Flags |= OFN_ALLOWMULTISELECT;

    // These can be used to find the offset in characters from the beginning
    // of the lpstrFile to the "File Title" (name plus extension, sans path)
    // and the extension (what follows the dot)
    //
    ofn.nFileOffset = 0;
    ofn.nFileExtension = 0;

    // Currently unused stuff.
    //
    ofn.lpstrDefExt = nullptr;
    ofn.lCustData = i_cast(LPARAM, nullptr);
    ofn.lpfnHook = nullptr;
    ofn.lpTemplateName = nullptr;

    BOOL ret;
    if (saving)
        ret = GetSaveFileName(&ofn);
    else
        ret = GetOpenFileName(&ofn);

    if (not ret) {
        DWORD cderr = CommDlgExtendedError();
        if (cderr == 0) {
            //
            // returned FALSE because of cancellation, that's fine, just
            // don't push anything to the data stack and we'll return blank
        }
        else if (cderr == FNERR_BUFFERTOOSMALL) // ofn.nMaxFile too small
            error = rebValue(
                "make error! -{dialog buffer too small for selection}-"
            );
        else
            error = rebValue(
                "make error! -{common dialog failure CDERR_XXX}-"
            );
    }
    else {
        if (not multi) {
            rebElide(
                "append", results, "local-to-file",
                    rebR(rebTextWide(chosen_utf16))
            );
        }
        else {
            const WCHAR *item_utf16 = chosen_utf16;

            size_t item_len = wcslen(item_utf16);
            assert(item_len != 0);  // must have at least one char for success
            if (wcslen(item_utf16 + item_len + 1) == 0) {
                //
                // When there's only one item in a multi-selection scenario,
                // that item is the filename including path...the lone result.
                //
                Value* item = rebLengthedTextWide(item_utf16, item_len);
                rebElide("append", results, "local-to-file", rebR(item));
            }
            else {
                // More than one item means the first is a directory, and the
                // rest are files in that directory.  We want to merge them
                // together to make fully specified paths.
                //
                Value* dir = rebLengthedTextWide(item_utf16, item_len);

                item_utf16 += item_len + 1;  // next

                while ((item_len = wcslen(item_utf16)) != 0) {
                    Value* item = rebLengthedTextWide(item_utf16, item_len);

                    rebElide(
                        "append", results,
                            "local-to-file join", dir, rebR(item)
                    );

                    item_utf16 += item_len + 1;  // next
                }

                rebRelease(dir);
            }
        }
    }

    rebFree(chosen_utf16);  // was always allocated

    rebFreeMaybe(filter_utf16);
    rebFreeMaybe(initial_dir_utf16);
    rebFreeMaybe(title_utf16);

  #elif defined(USE_GTK_FILECHOOSER)

    // gtk_init_check() will not terminate the program if gtk cannot be
    // initialized, and it will return TRUE if GTK is successfully initialized
    // for the first time or if it's already initialized.
    //
    int argc = 0;
    if (not gtk_init_check(&argc, nullptr))
        fail ("gtk_init_check() failed");

    // Note: FILTER not implemented in GTK for Atronix R3

    char *title_utf8 = rebSpellMaybe("title");

    // !!! Using a null parent causes console to output:
    // "GtkDialog mapped without a transient parent. This is discouraged."
    //
    GtkWindow *parent = nullptr;

    GtkWidget *dialog = gtk_file_chooser_dialog_new(
        title_utf8 == nullptr
            ? (saving ? "Save file" : "Open File")
            : title_utf8,
        parent,
        saving
            ? GTK_FILE_CHOOSER_ACTION_SAVE
            : GTK_FILE_CHOOSER_ACTION_OPEN,  // [SELECT_FOLDER CREATE_FOLDER]

        // First button and button response (underscore indicates hotkey)
        "_Cancel",
        GTK_RESPONSE_CANCEL,

        // Second button and button response
        saving ? "_Save" : "_Open",
        GTK_RESPONSE_ACCEPT,

        cast(const char*, nullptr)  // signal no more buttons
    );

    GtkFileChooser* chooser = GTK_FILE_CHOOSER(dialog);

    gtk_file_chooser_set_select_multiple(chooser, multi);

    char* initial_utf8 = rebSpellMaybe(initial);
    if (initial_utf8)
        gtk_file_chooser_set_current_folder(chooser, initial_utf8);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) != GTK_RESPONSE_ACCEPT) {
        //
        // If there was a cancellation, don't push any FILE!s to the stack.
        // A blank will be returned later.
    }
    else {
        // On success there are two different code paths, because the multi
        // file return convention (a singly linked list of strings) is not the
        // same as the single file return convention (one string).

        if (multi) {
            char* folder_utf8 = gtk_file_chooser_get_current_folder(chooser);

            if (folder == nullptr)
                error = rebValue(
                    "make error! -{folder can't be represented locally}-"
                );
            else {
                GSList *list = gtk_file_chooser_get_filenames(chooser);
                GSList *item;
                for (item = list; item != nullptr; item = item->next) {
                    //
                    // Filename is UTF-8, directory seems to be included.
                    //
                    // !!! If not included, `folder` is available to prepend.
                    //
                    rebElide("append", files, "as file!", rebT(item->data));
                }
                g_slist_free(list);

                g_free(folder_utf8);
            }
        }
        else {
            // filename is in UTF-8, directory seems to be included.
            //
            char *name_utf8 = gtk_file_chooser_get_filename(chooser);
            rebElide(
                "append", files, "as file!", rebT(name_utf8)
            );
            g_free(name_utf8);
        }
    }

    gtk_widget_destroy(dialog);

    rebFreeMaybe(initial_utf8);
    rebFreeMaybe(title_utf8);

    while (gtk_events_pending()) {
        //
        // !!! Commented out code here invoked gtk_main_iteration_do(0),
        // to whom it may concern who might be interested in any of this.
        //
        gtk_main_iteration();
    }

  #else
    UNUSED(saving);
    UNUSED(multi);

    error = rebValue(
        "make error! -{REQUEST-FILE only on GTK and Windows at this time}-"
    );
  #endif

    // The error is broken out this way so that any allocated strings can
    // be freed before the failure.
    //
    if (error)
        return rebDelegate("fail", rebR(error));

    if (rebUnboxLogic("empty?", results)) {
        rebRelease(results);
        return nullptr;
    }

    if (multi) {
        //
        // For the caller's convenience, return a BLOCK! if they requested
        // /MULTI and there's even just one file.  (An empty block might even
        // be better than null for that case?)
        //
        return results;
    }

    return rebValue("ensure file! first", rebR(results));
}


#if TO_WINDOWS
int CALLBACK ReqDirCallbackProc(
    HWND hWnd,
    UINT uMsg,
    LPARAM lParam,
    LPARAM lpData // counterintuitively, this is provided from bi.lParam
){
    UNUSED(lParam);

    const WCHAR* dir = p_cast(const WCHAR*, lpData);

    static bool inited = false;
    switch (uMsg) {
    case BFFM_INITIALIZED:
        if (dir)
            SendMessage(hWnd, BFFM_SETSELECTION, TRUE, i_cast(LPARAM, dir));
        SetForegroundWindow(hWnd);
        inited = true;
        break;

    case BFFM_SELCHANGED:
        if (inited and dir) {
            SendMessage(hWnd, BFFM_SETSELECTION, TRUE, i_cast(LPARAM, dir));
            inited = false;
        }
        break;
    }
    return 0;
}
#endif


//
//  export /request-dir*: native [
//
//  "Asks user to select a directory and returns it as file path"
//
//      return: [file!]
//      :title "Custom dialog title text"
//          [text!]
//      :path "Default directory path"
//          [file!]
//  ]
//
DECLARE_NATIVE(request_dir_p)
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

    Value* result = nullptr;
    Value* error = nullptr;

  #if defined(USE_WINDOWS_DIRCHOOSER)
    //
    // COM must be initialized to use SHBrowseForFolder.  BIF_NEWDIALOGSTYLE
    // is incompatible with COINIT_MULTITHREADED, the dialog will hang and
    // do nothing.
    //
    HRESULT hresult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (hresult == S_OK) {
        // Worked fine
    }
    else if (hresult == S_FALSE) {
        // Already initialized on this thread
    }
    else
        fail ("Failure during CoInitializeEx()");

    BROWSEINFO bi;
    bi.hwndOwner = nullptr;
    bi.pidlRoot = nullptr;

    WCHAR display[MAX_PATH];
    display[0] = '\0';
    bi.pszDisplayName = display; // assumed length is MAX_PATH

    WCHAR* title_utf16 = rebSpellWideMaybe("title");
    if (title_utf8)
        bi.lpszTitle = title_utf16;
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
    WCHAR* path_utf16 = rebSpellWideMaybe("path");
    bi.lParam = i_cast(LPARAM, path_utf16);  // nullptr uses default

    LPCITEMIDLIST pFolder = SHBrowseForFolder(&bi);

    WCHAR folder[MAX_PATH];
    if (pFolder == nullptr)
        assert(result == nullptr);
    else if (not SHGetPathFromIDList(pFolder, folder))
        error = rebValue("make error! -{SHGetPathFromIDList failed}-");
    else {
        result = rebValue("as file!", rebT(folder));
    }

    rebFreeMaybe(title_utf16);
    rebFreeMaybe(path_utf16);
  #else
    error = rebValue(
        "make error -{Temporary implementation of REQ-DIR only on Windows}-"
    );
  #endif

    if (error != nullptr)
        return rebDelegate("fail", rebR(error));

    return result;
}
