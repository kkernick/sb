# Usage

SB++ and Python-SB are generally similar, but the former has combined many features, removed others, and introduced some new functionality on top. Profiles created for one will likely not work for the other, but they are very similar and can be easily translated. See the examples folders in both implementations to see identical applications confined within both.

See Python-SB's [Usage Guide](../../python/docs/USAGE) for a description of the more general points; this document outlines SB++ libraries and major deviations from the former.

## Command Line Reference

As mentioned in the [README](../README.md), SB++ has a new argument handler that is more powerful than Python-SB. In essence, all arguments are treated as strings. For example, the verbose flag can either be:
*  Toggled with a long name: `--verbose`
* Toggled with a short name: `-v`
* Given an explicit value: `--verbose=debug` or `--verbose strace`
* Incremented: `-vv`, which corresponds to the *third* value in the switches valid list (IE `debug` for `--verbose`). The first value, typically `false`, is when the flag has not been provided, so `-v` will increment it to the second.
* Repeated, either to increment the value such as `--verbose --verbose` yielding `debug`, or to override older values such as `--verbose=false --verbose=error` yielding `error`. This is particularly useful for running a `.sb` profile from the command line, allowing you to change defined settings without modifying the script itself or the `sb.conf` file.
* Reset to the default value with `!`: `--verbose=!`

For lists, such as defining libraries:
* Values can be collected in a single invocation: `--libraries lib1.so lib2.so` or `--libraries lib1.so,lib2.so`
* Collected from multiple invocations: `--libraries lib1.so libraries lib2.so`
* Some lists, such as `libraries`, supports wildcard semantics: `--libraries lib*.so`.
* Some lists, such as `libraries`, support modifier semantics: `--libraries lib1.so:x` would exclude that library from being provided in the sandbox, regardless of dependency resolution.

***

* `--cmd/-C` specifies the program to run, and must be the first argument to which the switch can be omitted, such as `sb program`. You can use the switch to override the program, particularly for inherited profiles.
* `--app-dirs` specifies program folders to be provided to the sandbox:
	* `etc` provides `/etc/app`
	* `lib` provides `/usr/lib/app`
	* `share` provides `/usr/share/app`
	* `opt` provides `/opt/app`
* `--binaries` provides a list of additional binaries to provide in the sandbox
* `--destkop-entry/-D`  creates a desktop entry for the application. If needed, you can provide a desktop file explicitly in case it deviates from the program name, such as `--destkop-entry application.desktop`.
* `--devices` provides a list of devices to add to the sandbox
* `--dry` will create the sandbox, but not run the application, useful to warm the SOF.
* `--dry-startup` marks a profile as eligible to run by `sb.service`, which allows its SOF to be populated immediately. This remove the cold start penalty.
* `--electron` specifies the application as an Electron app, giving it access to GTK and NSS libraries, the ability to create user namespaces, and more. It can be provided with an explicit version if the app uses a system electron, such as `--electron 33
* `--env` provides a list of `KEY=VAL` environment variables to pass into the sandbox.
* `--file-passthrough` defines the default mode for files passed into the sandbox, either as unknown arguments (particularly when attached to a desktop file), or provided via `--files`:
	* `ro` mounts the files as read-only
	* `rw` mounts the files as read-write. Note that files are attached via bind-mounts, which means that the file cannot be moved or deleted. Some KDE applications update configuration files by creating a copy, then moving it into the original. If you use `--fs` instead of attaching host configurations, this isn't a problem.
	* `discard` only applies to directories, and uses an overlay to allow seamless, real read-write to the directory and its files, but to which writes are actually done to a temporary copy that is discarded upon program termination.
* `--files` provides a list of files to provide in the sandbox at `/enclave/path`. Modifier semantics allows for granular control over permissions, defaulting to the value of `--file-passthrough` (Which defaults to `false`, hence if `--file-passthrough` is not defined all files will need an explicit policy) such as `--files test.txt:ro test.csv:rw dir:discard`. You can also provide the unique modifiers `do` and `dw` for Direct Read-Only and Direct Read-Write, which will be exposed in the sandbox in the exact same path they appear on the host.

> [!note]
> This system has superseded `--ro` and `--rw`


* `--fs` provides the sandbox with a persistent root folder at `$XDG_DATA_HOME/sb/app/fs`, which is mounted onto the sandbox root. This allows for the additional of configuration files at `/etc`, binaries in `/usr/bin` (Which are dependency checked), and files in `/home`.  A modifier can be used to change the location of the `fs`, which is useful to have multiple profiles of an application, such as `--fs persist:b` for a `fs` at `$XDG_DATA_HOME/sb/app/b`
	* `cache` will mount the `fs` as temporary overlay. The sandbox can modify everything within, but changes will not be propagated to the `fs` on the host system.
	* `persist` will mount the `fs` read-write, allowing changes in the `fs` to persist across sessions.

> [!note]
> Applications like Chromium using Singletons to ensure only a single instance of the program is running. Using `persist` will mandate this requirement as all instance will share the `fs`, where `cache` will create a "copy" for each.

* `--gtk` provides GTK configurations to the sandbox, and sets `--gui`.
* `--gui` provides the Wayland socket, DRI device for graphics access, and fonts, themes, and other configurations needed for graphical applications.
* `--hardened-malloc` enforces the use of `libhardenedmalloc.so` within the sandbox; all processed, including the proxy, will use it.
* `--help` will print help information.
* `--include` provides system C/C++ headers for IDEs.
* `--libraries` provides a list of libraries to add to the sandbox, with dependencies recursively determined:
	* Libraries can outline a name: `liba.so`
	* Libraries can outline a path: `/usr/lib/liba.so`
	* Libraries can utilize wildcards: `/liba.so.*`
	* Libraries can be excluded with the `x` modifier: `liba.so:x`
* `--locale` provides the system locale, timezone, language to the sandbox.
* `--own` specifies portals that the application can *own*, or create new interfaces.
* `--pipewire` provides the Pipewire socket for audio/screen-sharing/video (Mediated via `--portals`)
* `--portals` provides a list of `org.freedesktop.portal.X` busses to provide to the sandbox including but not limited to:
	* `Desktop` is essential, and provides theme, font, and other settings.
	* `FileChooser` allows the program to select files from the host, which are provided to the sandbox via the interface, and is far more secure than explicitly provides `--files` since it doesn't require changing the script.
	* `Documents` is like `FileChooser`, but for folders, and allows it to keep a handle on the folder between sessions, such as saving your music folder for a music player between sessions. This "handle" is eventually dropped.
* `--post` specifies a command to run *after* the sandbox, and to which the sandbox will remain until the program dies. For example, services like `yarr` can launch a web browser pointing to itself, `--post chromium.desktop.sb:https://localhost:7070`, and when the Chromium instance closes, so does the sandbox. You can use the modifier as seen above to provide arguments to the command; wrap it in quotes to pass more than one.
* `--qt` provides Qt libraries and widgets to the sandbox:
	* `5` is for Qt 5 applications.
	* `6` is for Qt 6 applications
	* `kf6` is a super-set of `6` that provides KDE framework libraries as well. All settings, however, will pass KDE settings such as theme and fonts.
* `--script` will create a `.sb` script for the program, but not a desktop entry.
* `--seccomp` defines behavior of the SECCOMP Filter:
	* `false` will not use SECCOMP Filters to restrict syscalls.
	* `permissive` will create a blank filter if it does not exist at `$XDG_DATA_HOME/sb/app/filter.bpf`, or use the filter in a permissive mode where violations will be logged. You can use `sb-seccomp` to generate a filter in this mode, or just parse `/var/log/audit/audit.log` directly. Place permitted syscalls in the `$XDG_DATA_HOME/sb/app/syscalls.txt` file, but ensure the first line remains unchanged so that the hash can be used to regenerate when needed.
	* `enforcing` will enforce the BPF Filter, and syscalls not outlined will fail with `EPERM`. Most programs don't know how to handle a failed syscall, and will likely just crash.
	* `strace` will set `--verbose=strace` and collect the output from `strace` to determine syscalls and update the filter directly. You will likely still need to use `sb-seccomp`, as `strace` is not exhaustive, but this will create a good starting filter.

>[!tip]
> You will never need to scour logs or manually enter syscalls into `syscalls.txt`. `sb-seccomp` and `--seccomp=strace` are all you need to create a filter for *any* application, which will harden the security of the sandbox. It takes 30 seconds.

* `--see` specifies busses that the sandbox can *see*, but not interact with.
* `--share` specifies a list of *namespaces* that should be shared in the sandbox:
	* `none` shares nothing. Use this unless you have good reason to use another one.
	* `user` shares the user namespace, which is needed for Electron applications that create their own sub-sandboxes. It does *not* leak your username or home folder. `--electron` already adds this, so you don't need to explicitly specify it.
	* `ipc` shared the IPC namespace, which allows for consenting programs to communicate between the host/sandbox. It's largely unnecessary.
	* `pid` shares the PID namespace, such that new PIDs in the sandbox will not conflict with the host. It's largely unnecessary.
	* `net` shares the network namespace, which is required to connect to the internet and the local network. SB will add other essential networking configurations, such as `/etc/hosts` and SSL/TLS configuration and certificates.
	* `cgroup` shares the cgroup, allowing for regulation of memory and CPU consumption.
	* `all` shares all namespace. *Don't.*
* `--shell` provides `/usr/bin/sh` in the sandbox, and tells applications that it's the default shell for the user.
	* `true` merely provides the shell
	* `debug` will drop you *into* the shell, rather than running the app, so you can look around the sandbox for troubleshooting.
* `--sof` specifies the location for the SOF folder that contains libraries mounted to `/usr/lib` within the sandbox.
	* `tmp` will create the SOF at `/tmp/sb/app`, which is backed by ram.
	* `data` will create the SOF at `$XDG_DATA_HOME/sb/app/lib`. The libraries are persistent on disk, which means you'll need to occasional refresh it. However, since the SOF is self contained, this also allows you to run different versions of an app, irrespective of the hosts files.
	* `zram` will create the SOF at `/run/sb`, which should be a `zram` device mounted by the `sb.conf` zram generator service. This uses less RAM than `tmp`, and has comparable or better performance
	* `usr` will create the SOF at `/usr/share/sb`, which--being on the same file system as the host library folder, allows for direct hard-linking and thus zero storage overhead for storing the SOF. Additionally, this avoid the need to copy files to another system, which can dramatically increase cold boot speed:

| Profile (ms/MB) | `data` | `tmp` | `zram` | `usr` |
| --------------- | ------ | ----- | ------ | ----- |
| Chromium Cold   | 184.8  | 191.3 | 183.5  | 169.6 |
| Chromium Hot    | 3.2    | 2.9   | 3.0    | 3.1   |
| Chromium Update | 180.8  | 176.5 | 173.5  | 176.2 |
| Storage Usage   | 373    | 373   | 172    | 0M    |
| Backing         | Disk   | RAM   | RAM    | Disk  |

> [!warning]
> Race conditions can occur between the `sb.service` that populates the SOF on start, and other startup services. If you run a service confined by SB on startup, such as `syncthing`, either delay the service until *after* `sb.service` has run (Add `After=sb.service` to the service), or use `--sof=data`.

* `--startup` Don't use it.
* `--sys-dirs` System directories to mount into the sandbox:
	* `dev` binds `/dev`
	* `proc` binds `/proc`, but processes from outside the sandbox are invisible. If you don't enable `--share pid` the PID values will be different, too.
	* `lib` binds `/usr/lib`. You almost certainly don't need to do this unless you have a very stubborn application.
	* `bin` binds `/usr/bin`, see `lib`
	* `etc` binds `/etc`
	* `share` binds `/usr/share`
	* `var` binds `/var`
* `--talk` specifies busses that the sandbox can communicate over.
* `--update` specifies actions SB should take on sandbox generation.
	* `libraries` updates `lib.cache`, which is useful on updates.
	* `cache` updates binary and library caches in `$XDG_DATA_HOME/sb/cache`
	* `all` updates both
* `--verbose` toggles more information to be printed to the console
	* `log` prints logging information
	* `debug` prints a lot of debugging information, like every command run.
	* `error` runs the program under `strace`, but only filters errors.
	* `strace` runs the program under `strace`, errors and all.
* `--xdg-open` provides the `sb-open` script so that programs can open links with the default file handler outside the sandbox, such as opening a link in your web browser. **Don't use `--binaries xdg-open`**
