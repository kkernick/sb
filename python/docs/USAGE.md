# Usage

`sb` can be used directly from the command line, but it was intended to integrate within the Desktop Environment.

## Switches

This section contains an explanation for the various switches exposed by `sb`, grouped into logical sections based on functions.

### Portals

These switches pertain to the Flatpak Emulation with `xdg-dbus-proxy`. The usage of any of the below switches necesitates the creation of such a subordinate process for each instance of the program to mediate DBus calls, alongside running the application as the user to access the User Bus.

* `--portals` defines which portals should be exposed to the application, available at `org.freedesktop.portal`. Examples includes `FileChooser`, `Documents`, `Camera`, `OpenURI`, `Notifications`, and `Desktop`, alongside others.
* `--see` specifies busses that the application should be able to see, but not talk or own.
* `--talk` specifies busses that the application can communicate over.
* `--own` specifies busses the application owns.

#### `xdg-open`

To open files and URI's with the default file handler (outside of the sandbox), you can use the `--xdg-open` switch.This will pass the file to the host, which will then prompt which application to open the file in. Importantly, the application does not need to be in the caller's sandbox.

**Do not explicitly pass `xdg-open` via `--binaries`**. The program is a massive, monolithic shell script which `sb` cannot parse properly. Instead, the `--xdg-open` switch provides the `sb-open` binary, symlinked to the `xdg-open` open, which contains only the relevant functionality. This  reduces the attack surface by importing the functionality, and allows the required functionality to work within `sb`.

> [!note]
> The `--xdg-open` switch will also allow the application to communicate over the `OpenURI` portal, which is required to use this functionality.

### Files

These switches pertain to files and folders that should be exposed within the sandbox.

* `--ro` specifies a list of files and directories to pass to the application read-only.
* `--rw` specifies a list of files and directories to pass to the application read-write.
* `--app-dirs` specifies common locations for an application's data that should be passed into the sandbox:
	* `config`: `$XDG_CONFIG_HOME/app`; *Not recommended*, use `--home`.
	* `cache` `$XDG_CACHE_HOME/app`; *Not recommended*; use `--home`.
	* `etc`: `/etc/app`
	* `share`: `/usr/share/app`
	* `data`: `$XDG_DATA_HOME/app`; *Not recommended*; use `--home`.
	* `lib`: `/usr/lib/app`
* `--files` specifies a list of files and directories that should be available in the File Enclave.

#### Libraries

These switches pertain to shared library files.

* `--lib` will expose the entire `/usr/lib` folder to the application. *Not recommended*; SOF will be used if this flag is not provided.
* `--libraries` specifies additional libraries that SOF did not resolve. Supported formats include absolute path (`/usr/lib/mylib.so`), wildcard (`mylib*`), and both files and directories; the latter will be cached.

#### Binaries

These switches pertain to binaries.

* `--bin` will expose the entire `/usr/bin` folder to the application. *Not recommended*.
* `--binaries` specifies binary files to pass into the sandbox. Shell scripts will automatically be parsed to ensure the interpreter and dependencies are passed alongside the script.

#### Special Files

These switches pertain to special files and folders.

* `--sockets` expose important sockets and their configuration:
	* `wayland` exposes the Wayland socket.
	* `pipewire` exposes the Pipewire socket.

> [!warning]
> Exposing the Xorg socket is dangerous, and can allow for sandbox escape. Do not use it unless you must.

* `--dev` exposes the `/dev` folder. *Not recommended*.
* `--devices` specifies devices to pass into the sandbox.
* `--proc` exposes the `/proc` folder.

> [!info]
> The `/proc` in the sandbox only contains processes within the sandbox.

* `--etc` exposes the `/etc` folder. *Not recommended*.
* `--sys` exposes the `/sys` folder.
* `--usr-share` exposes the `/usr/share` folder. *Not recommended*.

### Environment

These switches pertain to the sandbox environment, and common use cases.

* `--share` specifies namespaces that should be shared with the sandbox:
	* `user` shares the user namespace (Used by electron, don't use otherwise).
	* `ipc` shares the IPC namespace (In case the application needs access to a SHM outside the sandbox)
	* `pid` shares the PID namespace (Process ID for the application will match the system, IE it won't start at 1)
	* `net` shares the Network namespaces and needed files for networking.
	* `uts` shares the UTS namespace (When enabled, the hostname cannot be modified)
	* `cgroup` shares the cgroup namespace (When disabled, hides the cgroup information).
	* `all` shares all namespaces. *Not recommended*.
	* `none` shared no namespaces

> [!info]
> Unless you know what you're doing, the only namespace you should consider is `net` for network access.

* `--env` specifies a list of `KEY=VALUE` environment variables for the sandbox
* `--dri` pulls files for a graphical application (DRI, fonts, icons, themes, etc)
* `--qt` is a superset of `--dri`, and pulls files needed for QT6 applications.
* `--qt5` is a superset of `--dri` and pulls files needed for QT5 applications.
* `--electron` is a superset of `--gtk` and pulls files needed for Electron applications
* `--electron-version` if your electron app uses the system electron, specify which version.
* `--python` specify a version of Python to provide to the sandbox.
* `--kde` is a superset of `--qt` and pulls KDE configuration.
* `--gtk` is a suprset of `--dri` and pulls in GTK3/4 configuration.
* `--shell` provides A shell to the sandbox.

> [!warning]
> `--shell` requires the use of `--dev`, which provides all devices to the sandbox. This can add considerable attack surface that may be unnecessary unless you need it. Additionally, it creates a fake `/etc/passwd` file to lie about the default shell `/usr/bin/sh`, which can conflict if you manually add the file (Which you shouldn't).

* `--include` provides C/C++ system headers, such as for `clangd`
*  `--locale` provides the system local and localization settings.
* `--hunspell` provides the hunspell dictionary for spell-checking.
* `--git` provides Git.
* `--hardened-malloc` provides and enforces the use of `libhardened-malloc`. *Strongly recommended*.

### Integration and Configuration

These switches pertain to integration in the Desktop Environment, and how the sandbox itself is operated.

* `--sof` specifies the location of the SOF dir for the application:
	* `tmpfs` will store the SOF at `/tmp`, on RAM.
	* `data` will store the SOF at `$XDG_DATA_HOME/sb`. *Not recommended*.
	* `zram` will store the SOF on a zstd-ramdisk at `/run/sb`. *Strongly recommended*.
* `--update-libraries` will force a regeneration of the library cache.
* `--update-cache` will force a regeneration of directory/script caches.
* `--share-cache` will share the system cache with the sandbox.
* `--file-passthrough {off,ro,rw}` specifies how file arguments should be handled:
	* `off` does not expose file arguments. *Not recommended*.
	* `ro` exposes files read-only.
	* `rw` exposes files read-write.
* `--make-desktop-entry` creates a copy of the applications desktop file, shadowing it so that when launched from the Desktop Environment, it runs sandboxed; a script is also created in `~/.local/bin`

> [!info]
> If the application file is local, (IE not in `/usr/share`), this file will be replaced, and a copy will be saved in `$XDG_DATA_HOME/sb`

* `--desktop-entry` specifies the name of the desktop file, if it does not match the program name.
* `--make-script` creates a `sb` script in `~/.local/bin` so that the sandboxed application can be run.

### FS

For a persistent state, you can use the `--fs` switch, which creates a per-application filesystem located at `$XDG_DATA_HOME/sb/$APP/fs`. This folder is overlain over the root of the sandbox. When `--fs=persist`, the folder will be created and mounted. Applications will create their configurations, which should involve the creation of `fs/home/sb`. This allows settings to persist over launches.

> [!info]
> Some applications may assume that the home folder exists, so you may need to create the `fs/home/sb` folder manually.

This scheme allows you to add any files you need to the sandbox, such as creating a `fs/usr/bin` folder to add binaries (Which you could also do via `--binaries`.

> [!warning]
> `/bin`, `/lib`, `/lib64`, and `/usr/lib64` are symlinked within the sandbox to `/usr/bin` and `/usr/lib` respectively. Attempts to create the above folders within the `fs` folder will yield to errors preventing `sb` from running the application. If you need to add a binary, use `--binaries`. If you need a library, use `--libraries`. If you *must* use the `fs` folder, create them in `/usr/bin` or `/usr/lib`.

### Execution

These switches pertain to program and sandbox execution

* `--debug-shell` drops the shell into `sh` within the sandbox, rather than run the application.
* `--strace` runs the application underneath `strace`.
* `--help` displays a help message.
* `--verbose` prints verbose information to the console.
* `--dry` generates the sandbox and caches, but doesn't run the application.
* `--startup` is used by the startup service. *Don't use it*.
* `--dry-startup` specifies the application should be dry-run on startup by the startup service.

## Profile Creation

To create a profile, there you first need to directly interface with `sb` until you have a functional sandbox, which is then saved and integrates into the desktop so that this sandbox is used when running the application in the Desktop Environment.

In a console, you can create the most basic sandbox imaginable:

`sb program`.

This will only contain SOF library dependencies, and probably won't work. To make profile creation easier, use `--strace` to see where failures occur, and what files are expected but missing, and `--verbose` to see what `sb` is doing. For simple, command line utilities, this may be enough, consider a functional sandbox for `yarr`:

`sb yarr --share net --proc --devices /dev/null`

However, for more complicated applications, you should consider:
* `--share net` If you need internet connectivity.
* `--dri`, `--gtk`, `--qt`, `--qt5`, `--electron` depending on your application to pull in configuration and libraries for GUI applications.
* `--home` if you want the application to remember configuration and state.
* `--sockets`, specifically:
	* `wayland` if you have a GUI application.
	* `pipewire` if you need audio/video support (Media, Screensharing, etc)
* `--app-dirs` if your application has configuration/files stored on the filesystem.

Once a sandbox has been created, use `--make-script` to create a script you can use in lieu of the application, or `--make-desktop-entry` if your application has an associated desktop file to shadow the system version with the sandboxed version in the local app folder.

### Recommendations

* Use `--strace` for profile creation.
* Use `--sof=zram`; it significantly reduces memory usage, and improves performance.
* Use `--hardened-malloc`.
* When creating profiles start broad, such as using `--sys`, `--etc`, `--usr-share` until the application launches, and then narrow down what files are needed where.
* Don't use `--lib` or `--bin`. The SOF works quite well.

## Configuration

You can set a default value to any of the above switches by creating a configuration file at `$XDG_CONFIG_HOME/sb/sb.conf`. In it, place case-insensitive keys, such as `HARDENED_MALLOC` or `sOf` with values, such as `True`, or `zram`, separated by `=`, and with each keypair separated by a newline:

```
SOF=zram
HARDENED_MALLOC=True
```

If a profile does not explicitly provide a value for one of these keys (If a profile specifies `--sof` it will override), the configuration value will be used instead of the default.

## SECCOMP

`sb` supports dynamic creation of BPF SECCOMP filters, which are then passed to the sandbox and enforce a subset of syscalls, reducing attack surface.

You can provide such syscalls either using the `--syscalls` switch or, and the preferred option, is to create a `syscalls.txt` file in the `$XDG_DATA_HOME/sb/$APP` folder. The format of the latter is space-delimited. For both, you can either provide the syscall name directly (IE `write`), or the numerical value (IE $0$).

SECCOMP Filters within `sb` subscribes to a Whitelisting approach, where only the specified syscalls are permitted within the sandbox; the exception to this is that if no `--syscalls` switch is provided, or the file does not exist, then no filtering will be done.

To ease the creation of these filters, you can use the `sb-seccomp` utility, so long as your system has the audit framework working. Pass it the `sb` script to run (It'll tack on `--seccomp-log` to the execution), and the name of the program (So it can dynamically update a `syscalls.txt` if it exists), and the program will spit out the syscalls that the program used. You may need to run it multiple times to capture all syscalls.

Once a list has been generated, you need not do anything to active the filter, so long as the `--seccomp-log` isn't activated. If you use `--verbose`, `sb` will inform you of the total permitted syscalls.

> [!note]
> Audit syscall names may not perfectly align with the real syscall names expected by SECCOMP. Particularly, `pread` and `pwrite` have numerical suffixes corresponding to architecture (IE `pread64`). These will not be reported by this script, and `sb` will error that the syscall is unrecognized. You may need to research the syscalls for your particular system to determine the actual name (Or, use the numerical value of the syscall instead)
p

## Lock File

Do prevent race conditions on multiple app launches, particularly app launches where the SOF is created, or the cmd/lib caches are updated, SB employs a lock file that exist in at `$SOF/$APP/sb.lock`, that exists for the brief time that the `bwrap` command needs to be generated or read from the cache. This file is then deleted upon the generation, allowing the next instance of the application to continue.

If, in the unlikely situation that a `sb` application is terminated/killed while it is in this generation, it may leave a lock in the SOF that will deadlock subsequent app launches. If applications fail to load, you may want to check if such a lock has persisted from a termination; if you use `zram` or `tmp` as the backing medium for the SOF this issue will be remedied through a reboot.

## Flags

Unknown arguments are passed to the application within the sandbox under the assumption that they are arguments for it; if an application is being run from the command line, these can be provided to either the `sb` call, or passed as arguments to an `.sb` file. In the latter case, these arguments and also be placed into the file itself. However, if you have many arguments, and don't want to include them into the `.sb` or command line, you can also create a `flags.conf` file in `$XDG_DATA_HOME/sb/$APP` folder, which will be sourced on execution. Formatting is arguments split by spaces or new lines.
