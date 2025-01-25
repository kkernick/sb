
syscall_groups = {

    # Sockets and Internet Connectivity
    "sockets": {"accept4", "bind", "connect", "getpeername", "getsockname", "getsockopt", "recvfrom", "recvmmsg", "recvmsg", "sendmmsg", "sendmsg", "sendto", "setsockopt", "socket", "listen", "shutdown", "socketpair"},

    # Read/Create/Write files
    "files_r": {"fcntl", "fstat", "statx", "lseek", "openat", "openat2", "poll", "ppoll", "pread64", "close", "close_range", "flock", "futex", "read", "access", "faccessat2", "pselect6", "getxattr", "epoll_ctl", "epoll_pwait", "epoll_wait", "epoll_create1", "readlinkat", "readlink"},
    "files_c": {"newfstatat", "set_robust_list", "dup", "dup2", "creat", "linkat"},
    "files_w": {"pwrite64", "write", "ftruncate", "fallocate", "renameat", "rename", "unlink", "unlinkat", "fadvise64", "fsync", "fdatasync", "umask", "symlink", "chmod", "fchmod"},

    # Directory Get/Set
    "dirs_g": {"getdents64", "getcwd"},
    "dirs_s": {"chdir", "mkdir"},

    # Memory Get/Set/Create
    "mem_g": {"madvise", "mincore"},
    "mem_s": {"mprotect", "mmap", "brk", "munmap", "mremap", "msync", "mlock"},

    # Fileystem Get/Set
    "fs_g": {"fstatfs", "fstat", "statfs"},
    "fs_s": {"setfsgid", "setfsuid"},

    # Signals
    "sig": {"rt_sigprocmask", "rt_sigreturn", "tgkill", "kill", "rt_sigaction", "sigaltstack"},

    # Scheduler Get/Set
    "sched_g": {"sched_yield", "sched_getaffinity", "getpriority", "sched_getparam", "sched_getscheduler"},
    "sched_s": {"sched_setaffinity", "sched_setscheduler", "setpriority"},

    # Info User/System
    "info": {"uname", "getuid", "geteuid", "getegid", "getpid", "getgid", "getresgid", "getresuid"},

    # Process Get/Set
    "process_g": {"exit", "exit_group", "wait4", "waitid", "prctl", "arch_prctl", "pidfd_open", "getpgrp", "getppid", "getpid", "gettid", "kcmp"},
    "process_s": {"setpgid", "setsid"},

    # Sub Process Spawning
    "spawn": {"clone", "clone3", "execve", "rseq", "set_tid_address", "vfork"},

    # INotify
    "inotify": {"inotify_init", "inotify_init1", "inotify_add_watch", "inotify_rm_watch"},

    # Sleeping
    "time": {"nanosleep", "clock_gettime", "clock_nanosleep"},

    # Timers
    "timers": {"timerfd_settime"},

    # Limits Get/Set
    "limits": {"getrlimit", "setrlimit", "prlimit64"},

    # Pkey
    "pkey": {"pkey_alloc", "pkey_mprotect"},

}
