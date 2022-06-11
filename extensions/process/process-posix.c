// !!! These functions were included in the process module but were only available
// on POSIX platforms.  They made use of a feature in the extension prep that would
// look in the native specs for what platforms the native should be enabled under.
// For simplicity that feature has been taken out at the moment.

#if TO_LINUX || TO_ANDROID || TO_POSIX || TO_OSX
static void kill_process(pid_t pid, int signal);
#endif

#if TO_LINUX || TO_ANDROID || TO_POSIX || TO_OSX

//
//  get-pid: native [
//
//  "Get ID of the process"
//
//      return: [integer!]
//  ]
//
REBNATIVE(get_pid)
{
    PROCESS_INCLUDE_PARAMS_OF_GET_PID;

    return rebInteger(getpid());
}



//
//  get-uid: native [
//
//  "Get real user ID of the process"
//
//      return: [integer!]
//  ]
//
REBNATIVE(get_uid)
{
    PROCESS_INCLUDE_PARAMS_OF_GET_UID;

    return rebInteger(getuid());
}


//
//  get-euid: native [
//
//  "Get effective user ID of the process"
//
//      return: [integer!]
//  ]
//
REBNATIVE(get_euid)
{
    PROCESS_INCLUDE_PARAMS_OF_GET_EUID;

    return rebInteger(geteuid());
}


//
//  get-gid: native [
//
//  "Get real group ID of the process"
//
//      return: [integer!]
//  ]
//
REBNATIVE(get_gid)
{
    PROCESS_INCLUDE_PARAMS_OF_GET_UID;

    return rebInteger(getgid());
}


//
//  get-egid: native [
//
//  "Get effective group ID of the process"
//
//      return: [integer!]
//  ]
//
REBNATIVE(get_egid)
{
    PROCESS_INCLUDE_PARAMS_OF_GET_EUID;

    return rebInteger(getegid());
}


//
//  set-uid: native [
//
//  {Set real user ID of the process}
//
//      return: "Same ID as input"
//          [integer!]
//      uid {The effective user ID}
//          [integer!]
//  ]
//
REBNATIVE(set_uid)
{
    PROCESS_INCLUDE_PARAMS_OF_SET_UID;

    if (setuid(VAL_INT32(ARG(uid))) >= 0)
        return_value (ARG(uid));

    switch (errno) {
      case EINVAL:
        fail (PAR(uid));

      case EPERM:
        Fail_Permission_Denied();

      default:
        rebFail_OS(errno);
    }
}


//
//  set-euid: native [
//
//  {Get effective user ID of the process}
//
//      return: "Same ID as input"
//          [<opt>]
//      euid "The effective user ID"
//          [integer!]
//  ]
//
REBNATIVE(set_euid)
{
    PROCESS_INCLUDE_PARAMS_OF_SET_EUID;

    if (seteuid(VAL_INT32(ARG(euid))) >= 0)
        return_value (ARG(euid));

    switch (errno) {
      case EINVAL:
        fail (PAR(euid));

      case EPERM:
        Fail_Permission_Denied();

      default:
        rebFail_OS(errno);
    }
}


//
//  set-gid: native [
//
//  {Set real group ID of the process}
//
//      return: "Same ID as input"
//          [<opt>]
//      gid "The effective group ID"
//          [integer!]
//  ]
//
REBNATIVE(set_gid)
{
    PROCESS_INCLUDE_PARAMS_OF_SET_GID;

    if (setgid(VAL_INT32(ARG(gid))) >= 0)
        return_value (ARG(gid));

    switch (errno) {
      case EINVAL:
        fail (PAR(gid));

      case EPERM:
        Fail_Permission_Denied();

      default:
        rebFail_OS(errno);
    }
}


//
//  set-egid: native [
//
//  "Get effective group ID of the process"
//
//      return: "Same ID as input"
//          [integer!]
//      egid "The effective group ID"
//          [integer!]
//  ]
//
REBNATIVE(set_egid)
{
    PROCESS_INCLUDE_PARAMS_OF_SET_EGID;

    if (setegid(VAL_INT32(ARG(egid))) >= 0)
        return_value (ARG(egid));

    switch (errno) {
      case EINVAL:
        fail (PAR(egid));

      case EPERM:
        Fail_Permission_Denied();

      default:
        rebFail_OS(errno);
    }
}



//
//  send-signal: native [
//
//  "Send signal to a process"
//
//      return: <none>  ; !!! might this return pid or signal (?)
//      pid [integer!]
//          {The process ID}
//      signal [integer!]
//          {The signal number}
//  ]
//
REBNATIVE(send_signal)
{
    PROCESS_INCLUDE_PARAMS_OF_SEND_SIGNAL;

    pid_t pid = rebUnboxInteger(ARG(pid));
    int signal = rebUnboxInteger(ARG(signal));

    // !!! Is called `send-signal` but only seems to call kill (?)
    //
    kill_process(pid, signal);

    return Init_None(OUT);
}

#endif  // TO_LINUX || TO_ANDROID || TO_POSIX || TO_OSX
