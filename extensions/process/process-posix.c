// !!! These functions were included in the process module but were only available
// on POSIX platforms.  They made use of a feature in the extension prep that would
// look in the native specs for what platforms the native should be enabled under.
// For simplicity that feature has been taken out at the moment.

#if TO_LINUX || TO_ANDROID || TO_POSIX || TO_OSX || TO_HAIKU
static void kill_process(pid_t pid, int signal);
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
DECLARE_NATIVE(get_pid)
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
DECLARE_NATIVE(get_uid)
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
DECLARE_NATIVE(get_euid)
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
DECLARE_NATIVE(get_gid)
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
DECLARE_NATIVE(get_egid)
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
//      uid {The effective user ID}
//          [integer!]
//  ]
//
DECLARE_NATIVE(set_uid)
{
    INCLUDE_PARAMS_OF_SET_UID;

    if (setuid(VAL_INT32(ARG(uid))) >= 0)
        return COPY(ARG(uid));

    switch (errno) {
      case EINVAL:
        fail (PARAM(uid));

      case EPERM:
        Fail_Permission_Denied();

      default:
        rebFail_OS(errno);
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
DECLARE_NATIVE(set_euid)
{
    INCLUDE_PARAMS_OF_SET_EUID;

    if (seteuid(VAL_INT32(ARG(euid))) >= 0)
        return COPY(ARG(euid));

    switch (errno) {
      case EINVAL:
        fail (PARAM(euid));

      case EPERM:
        Fail_Permission_Denied();

      default:
        rebFail_OS(errno);
    }
}


//
//  /set-gid: native [
//
//  "Set real group ID of the process"
//
//      return: "Same ID as input"
//          [~null~]
//      gid "The effective group ID"
//          [integer!]
//  ]
//
DECLARE_NATIVE(set_gid)
{
    INCLUDE_PARAMS_OF_SET_GID;

    if (setgid(VAL_INT32(ARG(gid))) >= 0)
        return COPY(ARG(gid));

    switch (errno) {
      case EINVAL:
        fail (PARAM(gid));

      case EPERM:
        Fail_Permission_Denied();

      default:
        rebFail_OS(errno);
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
DECLARE_NATIVE(set_egid)
{
    INCLUDE_PARAMS_OF_SET_EGID;

    if (setegid(VAL_INT32(ARG(egid))) >= 0)
        return COPY(ARG(egid));

    switch (errno) {
      case EINVAL:
        fail (PARAM(egid));

      case EPERM:
        Fail_Permission_Denied();

      default:
        rebFail_OS(errno);
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
DECLARE_NATIVE(send_signal)
{
    INCLUDE_PARAMS_OF_SEND_SIGNAL;

    pid_t pid = rebUnboxInteger(ARG(pid));
    int signal = rebUnboxInteger(ARG(signal));

    // !!! Is called `send-signal` but only seems to call kill (?)
    //
    kill_process(pid, signal);

    return NOTHING;
}

#endif  // TO_LINUX || TO_ANDROID || TO_POSIX || TO_OSX || TO_HAIKU
