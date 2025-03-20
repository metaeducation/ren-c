// !!! These functions were included in the process module but were only available
// on POSIX platforms.  They made use of a feature in the extension prep that would
// look in the native specs for what platforms the native should be enabled under.
// For simplicity that feature has been taken out at the moment.

#if TO_LINUX || TO_ANDROID || TO_POSIX || TO_OSX || TO_HAIKU
static Bounce Delegate_Kill_Process(pid_t pid, int signal);
#endif

#if TO_LINUX || TO_ANDROID || TO_POSIX || TO_OSX || TO_HAIKU

//
//  /get-pid: native [
//
//  "Get ID of the process"
//
//      return: [integer!]
//  ]
//
DECLARE_NATIVE(GET_PID)
{
    INCLUDE_PARAMS_OF_GET_PID;

    return rebInteger(getpid());
}



//
//  /get-uid: native [
//
//  "Get real user ID of the process"
//
//      return: [integer!]
//  ]
//
DECLARE_NATIVE(GET_UID)
{
    INCLUDE_PARAMS_OF_GET_UID;

    return rebInteger(getuid());
}


//
//  /get-euid: native [
//
//  "Get effective user ID of the process"
//
//      return: [integer!]
//  ]
//
DECLARE_NATIVE(GET_EUID)
{
    INCLUDE_PARAMS_OF_GET_EUID;

    return rebInteger(geteuid());
}


//
//  /get-gid: native [
//
//  "Get real group ID of the process"
//
//      return: [integer!]
//  ]
//
DECLARE_NATIVE(GET_GID)
{
    INCLUDE_PARAMS_OF_GET_UID;

    return rebInteger(getgid());
}


//
//  /get-egid: native [
//
//  "Get effective group ID of the process"
//
//      return: [integer!]
//  ]
//
DECLARE_NATIVE(GET_EGID)
{
    INCLUDE_PARAMS_OF_GET_EUID;

    return rebInteger(getegid());
}


//
//  /set-uid: native [
//
//  "Set real user ID of the process"
//
//      return: "Same ID as input"
//          [integer!]
//      uid "The effective user ID"
//          [integer!]
//  ]
//
DECLARE_NATIVE(SET_UID)
{
    INCLUDE_PARAMS_OF_SET_UID;

    if (setuid(VAL_INT32(ARG(UID))) >= 0)
        return COPY(ARG(UID));

    switch (errno) {
      case EINVAL:
        return FAIL(PARAM(UID));

      case EPERM:
        return Delegate_Fail_Permission_Denied();

      default:
        return FAIL(rebError_OS(errno));
    }
}


//
//  /set-euid: native [
//
//  "Get effective user ID of the process"
//
//      return: "Same ID as input"
//          [~null~]
//      euid "The effective user ID"
//          [integer!]
//  ]
//
DECLARE_NATIVE(SET_EUID)
{
    INCLUDE_PARAMS_OF_SET_EUID;

    if (seteuid(VAL_INT32(ARG(EUID))) >= 0)
        return COPY(ARG(EUID));

    switch (errno) {
      case EINVAL:
        return FAIL(PARAM(EUID));

      case EPERM:
        return Delegate_Fail_Permission_Denied();

      default:
        return FAIL(rebError_OS(errno));
    }
}


//
//  /set-gid: native [
//
//  "Set real group ID of the process"
//
//      return: "Same ID as input"
//          [integer!]
//      gid "The effective group ID"
//          [integer!]
//  ]
//
DECLARE_NATIVE(SET_GID)
{
    INCLUDE_PARAMS_OF_SET_GID;

    if (setgid(VAL_INT32(ARG(GID))) >= 0)
        return COPY(ARG(GID));

    switch (errno) {
      case EINVAL:
        return FAIL(PARAM(GID));

      case EPERM:
        return Delegate_Fail_Permission_Denied();

      default:
        return FAIL(rebError_OS(errno));
    }
}


//
//  /set-egid: native [
//
//  "Get effective group ID of the process"
//
//      return: "Same ID as input"
//          [integer!]
//      egid "The effective group ID"
//          [integer!]
//  ]
//
DECLARE_NATIVE(SET_EGID)
{
    INCLUDE_PARAMS_OF_SET_EGID;

    if (setegid(VAL_INT32(ARG(EGID))) >= 0)
        return COPY(ARG(EGID));

    switch (errno) {
      case EINVAL:
        return FAIL(PARAM(EGID));

      case EPERM:
        return Delegate_Fail_Permission_Denied();

      default:
        return FAIL(rebError_OS(errno));
    }
}



//
//  /send-signal: native [
//
//  "Send signal to a process"
//
//      return: [~]  ; !!! might this return pid or signal (?)
//      pid "The process ID"
//          [integer!]
//      signal "The signal number"
//          [integer!]
//  ]
//
DECLARE_NATIVE(SEND_SIGNAL)
{
    INCLUDE_PARAMS_OF_SEND_SIGNAL;

    pid_t pid = rebUnboxInteger(ARG(PID));
    int signal = rebUnboxInteger(ARG(SIGNAL));

    // !!! Is called `send-signal` but only seems to call kill (?)
    //
    return Delegate_Kill_Process(pid, signal);
}

#endif  // TO_LINUX || TO_ANDROID || TO_POSIX || TO_OSX || TO_HAIKU
