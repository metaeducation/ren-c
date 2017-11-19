//
//  File: %reb-file.h
//  Summary: "Special file device definitions"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
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

// RFM - REBOL File Modes
enum {
    RFM_READ = 1 << 0,
    RFM_WRITE = 1 << 1,
    RFM_APPEND = 1 << 2,
    RFM_SEEK = 1 << 3,
    RFM_NEW = 1 << 4,
    RFM_READONLY = 1 << 5,
    RFM_TRUNCATE = 1 << 6,
    RFM_RESEEK = 1 << 7, // file index has moved, reseek
    RFM_DIR = 1 << 8
};

// RFE - REBOL File Error
enum {
    RFE_BAD_PATH,
    RFE_NO_MODES, // No file modes specified
    RFE_OPEN_FAIL, // File open failed
    RFE_BAD_SEEK, // Seek not supported for this file
    RFE_NO_HANDLE, // File struct has no handle
    RFE_NO_SEEK, // Seek action failed
    RFE_BAD_READ, // Read failed (general)
    RFE_BAD_WRITE, // Write failed (general)
    RFE_DISK_FULL, // No space on target volume
    RFE_MAX
};

#define MAX_FILE_NAME 1022
