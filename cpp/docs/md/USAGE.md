# Usage

```bash
sb program
```

Unless your program is simple, this likely won't work by itself. SB++ subscribers to a whitelisting approach where the sandbox is populated with nothing, and it is on the profile to explicitly provide files and configurations.

>[!tip]
>AppArmor and SB++ work very well together, and provides defense-in-depth.

## Switch Semantics

SB uses a custom argument handler for its command line switches. It is very powerful, but bears a brief aside to explain its grammar.

Switches can be one of seven discrete classes:
* True/False, such as `--pipewire` which is either on or off.
* Discrete, such as `--sof` which is either `usr`, `tmp`, `data`, or `zram`.
* Custom, such as `--script` take a single, arbitrary value.
* Optional, such as `--electron` is treated as TF, but can be provided an optional, arbitrary value.
* Lists, such as `--libraries` which can accept multiple arbitrary values.
* Flags, which is a List with explicit values, such as `--qt`, which takes multiple values, but only those of `false`, `5`, `6`, or `kf6`.
* Modifiers, which takes a key-pair separated by a `:`, such as `--fs`, which is a Discrete switch taking either `false`, `persist`, or `cache`, but which can optionally be modified, such as `--fs persist:md` to specify a folder to use as the filesystem backing. Modifiers can also be Lists, such as `--files` accepting multiple arbitrary values, to which each can be modified.

>[!note]
>This document will specify the type of switch via `TF`, `D`, `O`, `L`, `F`, `+M` when they appear in the documentation.

Internally, all switches are either strings, or lists. It does this via two novel designs:
* `false` is an explicit value, and the default for most switches.
* Switches increment through their valid values.

This means that a True/False switch can be toggled in two different ways:
* `--pipewire`, which *increments* the PipeWire flag from `false` to `true`
* `--pipewire=true` or `--pipewire true`, which explicitly sets the value to one of the two valid selections, being `false` or `true`.

This further means that every switch can be increments, moving it from valid values. `--verbose`'s valid values (As seen in `--help`) are ordered as `{false, log, debug, error, strace}`. That means, if `--verbose/-v` is not provided, it is set as `false`. However, if you pass `-vv`, it will step to `debug`, and `-vvvv` will set it to `strace`.

One of the most powerful features of the argument handler is that it can override earlier invocations of a switch. For example, if a profile specifies `--fs=cache` in its script, but you need to set it to `persist` temporarily, rather than having to modify the script, you can just pass `profile.sb.desktop --fs=persist` to override it.

For lists, which by default append values together a la `--libraries A --libraries B`, you can provide `!` to set it to the default, clear value. This works for *any* switch, and sets it to its default.

The above functionality works together for *drop-ins*, where you can have a SB script call *another* SB script as its program, which will merely adopt the callee's settings. For example:
* If you already have a profile for Chromium, but want a *second* profile that doesn't have a home folder, you can create a second profile that merely looks like `sb chromium.desktop.sb --fs=false`.
* If you want to create multiple instances of an application, each using their own home, you can merely create a single, main script, and then create specialized ones via `sb profile.desktop.sb --fs=cache:instance`, where *instance* is the home folder for that particular copy.

> [!note]
> All switches that pass files to them, such as `--libraries` or `--files`, will check if the file exists, and won't tear down `bwrap` if it doesn't. This means conditionally available files will either be passed if they exist, or ignored if they don't.


## Debugging

The `--verbose (L)` flag is absolutely essential for constructing a profile, as it provides invaluable information about how the program runs within the sandbox:
* `--verbose debug` will print`xdg-dbus-proxy` output to the console, allowing you to see what busses the program attempts to connect to. It also prints what SB is doing, what it adds, and every command run.
* `--verbose error` will run the program under `strace`, capturing all errors the program runs into, such as attempts to access files that don't exist.
* `--verbose strace` will run the program under `strace`, capturing *all* syscalls from the program, errors and otherwise.

`--shell debug (D)` will construct the sandbox exactly as if the switch was not provided, but instead of running the actual program, will instead drop into a debug shell. This will allow you to navigate the sandbox and find issues.

>[!tip]
>Use `--binaries (L)` to bring in command line utilities, such as `cat`, `less`, and `ls`

The `--update (D+M)` flag is used to invalidate caches used by the program, useful when you are iteratively building a profile. The following values are supported:
* `--update libraries` will update library caches.
* `--update cache` will update library and binary caches.
* `--update all` will update everything cached.
* `--update clean` will update everything cached, and delete any old configurations.

>[!note]
>SB automatically silos separate configurations of the program, so you likely don't need to manually invalidate caches. However, this will create multiple instances in the SOF, so `--update clean` is a good idea to remove old, unused versions.

## GUI

You're likely try to run a graphical application within SB++, and as such you'll want to use one of the switches dedicated for those kinds of applications.
* `--electron (O)` is for Electron/Chromium apps, such as Chrome and Obsidian. You can optionally provide an electron version, if your application uses the system electron, such as `--electron 28`.  It also enables `--gtk 3`.
* `--qt (F)` is for Qt applications, such as KDE programs. There are three flags for the switch:
	* `--qt 5` is for Qt 5 applications.
	* `--qt 6` is for Qt 6 applications.
	* `--qt kf6` is a super-set of `--qt 6`, providing KDE Framework libraries that are needed by some KDE programs.
* `--gtk (F)` is for GTK applications. It's a *flag* switch, meaning you can provide multiple discrete flags that will add functionality, such as `--gtk 3 4 gdk`. Valid options include:
	* `3` for GTK3
	* `4` for GTK4
	* `gdk` for the Pixel Buffer, which is needed for some apps to draw icons.
	* `sourceview` for the GTK Source View, required by some text editors.
	* `adwaita` for `libadwaita` applications
	* `gir` for the GIR Repository.
	* `webkit` for WebKit
	* `gst` for GStreamer.
* `--gui (F)` is for any application that doesn't fall into the above switches. Note that all above flags already enable `--gui` if it isn't set, but as a *flag* switch you can pass further values, which includes:
	* `vulkan` for Vulkan support
	* `vaapi` for hardware accelerated video decoding/encoding.

The `--desktop-entry (O)` switch can be used to shadow the system `.desktop` file used to launch the application, ensuring that your Window Manager launches the application sandboxed. It creates a script file in `~/.local/bin`, and places the override in `$XDG_DATA_HOME/applications`. If the desktop file does not match the program name, you'll need to provide the desktop file name (In `/usr/share/applications`),  such as `--desktop-entry org.kde.okular.desktop`.

If your program doesn't have a desktop file, you can use `--script (C)` to create the script file in `~/.local/bin`.

All the above flags assume the program is run under Wayland, which is a far more secure protocol than Xorg. Providing the Xorg socket into the sandbox is such as easy escape that you might as well not bother with SB. However, if you have `xorg-server-xephyr` installed, SB can run an isolated Xorg server specifically for that application, via the `--xorg (F)` switch. 

> [!note]
> While `--xorg` *creates* the server and corresponding `DISPLAY` environment variable, it doesn't provide any graphical libraries, and as such you'll need to include something like`--gui` or `--electron` depending on the app. 

>[!tip]
>Wayland will *always* be available within the sandbox, so long as `--gui` is directly or indirectly provided. Therefore, if your program runs under both, and defaults to Wayland, you'll need to somehow tell it to use X11, such as `--ozone-platform=x11` for Chromium/Electron

`--xorg` accepts the following flags:
*  `--xorg resize` allows the Xorg window to be resized (This is *not* the application window, but the *server's*)
* `--xorg fullscreen` makes the Xorg window fullscreen.
* `--xorg gl` provides OpenGL extensions and support. YMML on Hardware Acceleration.

You can also use the `--xsize (C)` switch to provide a `width X height` argument, such as `--xsize 1000x1000`. This takes precedent over `--fullscreen`.
## Portals

If you have `xdg-dbus-proxy` installed, you can provide Portals and Busses that the application can communicate to the host over. There are four such switches exposed:
* `--portals (L)` Is for FreeDesktop Portals, such as:
	*  `Desktop`: For system settings (Like theme, fonts, etc).
	* `FileChooser`: To select a file on the host, and pass is into the sandbox.
	* `Documents`: A more persistent `FileChooser`
	* `Camera`: To provide the Camera via Pipewire.
	* `Notifications`: For system notifications.
* `--own (L)` is for busses that the program *owns*, which is most frequently needed for GTK applications, such as `--own io.bassi.Amberol`, or Media Notifications such as `--own org.mpris.MediaPlayer2.chromium.instance2`
* `--talk (L)` is for busses that the program can talk to.
* `--see (L)` is for busses the program can see, but can't interact with.

To open files outside of the sandbox, and in the host's default application, use `--xdg-open (TF)`.
## Sound

Use `--pipewire (TF)` For sound support. If your program uses GStreamer, use `--gtk gst`.

## Libraries

Usually, SB will automatically find all needed libraries used by the program, but if it can't, you have two options:
* Use `--libraries (L+M)` to pass them to the sandbox. It accepts three formats:
	* Wildcards: `--libraries libSDL3*`
	* Directories: `--libraries /usr/lib/chromium`
	* Files: `--libraries /usr/lib/libSDL3.so`. 
	As a list switch, you can pass multiple such values, either in one invocation, like `--libraries A B`, or multiple, such as `--libraries A --libraries B`.
* Use `--sys-dirs lib` to pass `/usr/lib` into the sandbox. Don't do this unless you have no choice.

> [!tip]
> Use `--verbose=error` to find libraries the program tried to access, but didn't exist!

### SOF

Rather than providing the entire `/usr/lib` folder, SB recursively finds all libraries needed to run the program, and stores them into a per-application *SOF* folder, which is then provided as the sandbox's `/usr/lib`. Unless you provide `--sys-dirs lib`, which you **shouldn't**, this algorithm will:
* Use `ldd` to find all libraries used by the program, including its dependencies.
* Parse shell scripts to determine all binaries used within it.

You can use the `--sof` switch to specify where this folder will be stored on disk:
* `--sof usr` is the *Recommended* option, as it is the fastest (There is no copying) and most space efficient (0 overhead, as it merely makes hard-links to the system libraries), stored at `/usr/share/sb/program`. *However* if you have protected hard-links enabled, this will not work, but all other options are so bad comparatively you should probably just use `sysfs fs.protected_hardlinks=0` and use it.
* `--sof tmp` will place the SOF in `/tmp/sb/program`. Backing onto ram, it will be fast to setup, and will use a shared folder to share libraries between other profiles, but there will be a space overhead equal to all libraries used by all profiles without duplication. However, because it is stored on RAM, it will be cleared on reboot, and hence there will be a cold-boot penalty that can be somewhat remedied via the startup service.
* `--sof data` will place the SOF in `$XDG_DATA_HOME/sb/program`. It backs onto disk, which means it will be slower to setup, but there will be no cold-boot penalty on startup. It uses the same shared library as `tmp`, and as such the same space requirement.
* `--sof zram` is only available if the zram configuration is enabled and `zram-generator` is installed, backing to `/run/sb`. Being stored on a compressed ram disk, it is faster and more space efficient than either `tmp` or `data`, but suffers a cold-boot penalty as with `tmp` as it backs to RAM. If you cannot use `usr`, and have `zram-generator`, this backing method is the preferred one.

The following table summarizes each option:

| Backing | Backing Medium | Space Cost | Startup Cost | Compatability              |
| ------- | -------------- | ---------- | ------------ | -------------------------- |
| `data`  | Disk           | Shared     | No           | All                        |
| `tmp`   | RAM            | Shared     | Yes          | All                        |
| `zram`  | RAM            | Shared     | Yes          | `zram-generator`           |
| `usr`   | Disk           | 0          | No           | `fs.protected_hardlinks=0` |
For a more quantifiable example, consider the following benchmark using `examples/chromium.desktop.sb` (`./benchmark.sh cpp main chromium.desktop.sb "" --sof=data`)

| Backing | Cold     | Hot    | Libraries | Cache    | Space Overhead |
| ------- | -------- | ------ | --------- | -------- | -------------- |
| `data`  | 248320.4 | 4578.8 | 11515.7   | 258139.1 | 378.3MB        |
| `tmp`   | 236653.2 | 4215.3 | 10965.7   | 253466.1 | 378.3MB (RAM)  |
| `zram`  | 232735.7 | 4184.4 | 11024.5   | 251254.3 | 183.0MB (RAM)  |
| `usr`   | 238148.9 | 3317.5 | 10119.9   | 246266.4 | 0              |
> [!note]
> *Cold* in this case involves deleting the folder before each run. This simulates `--update all`, but note that `data` and `usr` will launch at *Hot* at every invocation, whereas `tmp` and `zram` will launch at *Cold* once after reboot, then *Hot*. If the numbers did not make this obvious, you should use the Dry Startup service if you use either of the RAM backed SOFs.
> 

>[!warning]
>YMML on speed between RAM and Disk backings depending on your own RAM and Disk/Filesystem. These tests were conducted on ZFS, which effectively makes RAM/Disk the same speed
#### Dry Start

If your SOF is backed to RAM, you may want to consider the Dry Startup, which runs on user login and populates the SOF of all profiles in `~/.local/bin` with the `--dry-startup` switch. Unless you have limited RAM, it's recommended to simply put `DRY_STARTUP` in `$XDG_CONFIG_HOME/sb.conf` to populate all profiles on boot.

You'll need to install the `sb.service`, and enable it via `systemctl enable --user sb`.
## Binaries

If you need to add binaries to the sandbox, You can either use:
* `--binaries (L)`, which will use `which` to resolve the location
* `--sys-dirs bin`, which you shouldn't use.

This is useful if you're in a debug shell, such as `sb program --shell debug --binaries ls cat`

## File Passthrough

There are several switches dedicated to passing arbitrary files into the sandbox.

Firstly, *Devices* can be provided via `--devices (L)`, such as `--devices /dev/tty`. You can also use `--sys-dirs dev`, but you shouldn't.

For regular files, there two ways to pass files through:
* With the `--files (L+M)` switch
* Simply passing the file through without a switch. This mode is mostly for Desktop Files, as it allows you to open files the sandboxed program supports with your file manager or other applications.

For both of these methods, you must specify a pass through mode. This is done via `--file-passthrough (D)`, which, if not set, ignores both means of passing files through. Valid options are:
* `--file-passthrough ro` to pass files Read Only
* `--file-passthrough rw` to pass files Read+Write.
* `--file-passthrough discard`, only applicable if passing directories, where the sandbox overlays the directories with a temporary overlay, such that the program has `rw` access, but that such changes don't actually appear on the host.

When using `--files (L+M)`, you can modify each file via a modifier, to which you have four options:
* `--files file.txt:ro` to pass the file Read Only
* `--files file.txt:rw` to pass the file Read+Write
* `--files file.txt:do` to pass the file Direct Read-Only
* `--files file.txt:dw` to pass the file Direct Read-Write.

By default, and to make AppArmor profile generation easier, all passed files are actually exposed in a `/enclave` folder in the sandbox, such that profiles can just have `/enclave/{,**} rw,` in their AppArmor profiles. However, if you need the path to exist as it does on the host, such as passing something from `/etc` or `/var`, you can provide the `do/dw` modifier accordingly.

If no modifier is provided, it defaults to whatever `--file-passthrough` is, so you can have something like: `--file-passthrough ro --files text.txt text.csv:rw /etc/hosts:do`. 

## Directory Passthrough

While you can use the above `--files` mechanism to pass folders to the sandbox, there are some special flags dedicated to special folders


`--app-dirs (F)` Allows you to specify folders that are owned by the program, and thus share the same name as the program, to save typing it out. For example, with the program `chromium`:
* `--app-dirs etc` provides `/etc/chromium`
* `--app-dirs lib` provides `/usr/lib/chromium` and resolves libraries within as if it were provided via `--libraries`
* `--app-dirs share` provides `/usr/share/chromium`.
* `--app-dirs opt` provides `/opt/chromium`, and resolves libraries within as if it were provided via `--libraries`.

`--sys-dirs (F)` Allows you to specify system directories, some of which may need to be mounted in ways `--files` cannot accommodate:

>[!warning]
>Many of the options to `--sys-dirs` open tremendous attack surface to the sandbox, and should **only** be used for debugging and sandbox generation. Use `--files`!

* `--sys-dirs dev` mounts `/dev` as a `DevFS`. This shares all devices with the system. Don't use it.
* `--sys-dirs proc`  mounts `/proc` as a `ProcFS`

> [!note] Providing `--sys-dirs proc` provides the sandbox with a `/proc` folder, but it is *not* the system. The sandbox will never be able to access the processes of programs running on the host.

* `--sys-dirs lib` mounts `/usr/lib`. Don't use it.
* `--sys-dirs bin` mounts `/usr/bin`. Don't use it.
* `--sys-dirs etc` mounts `/etc`. Don't use it.
* `--sys-dirs share` mounts `/usr/share`. Don't use it.
* `--sys-dirs var` mounts `/var` Don't use it.

### Local Filesystem

Passing your home folder into the sandbox is a terrible idea. **Never** use `--files /home`, and ***especially*** not `--files /home:rw`. Like providing the global Xorg socket, you might as well save the effort and run the program unconfined. However, applications store their configuration files in the home, and unless you want a clean slate every launch, you still need a way to store them.

The `--fs` switch will create an isolated directory in `$XDG_DATA_HOME/sb/program` that is overlain on the *root* of the sandbox. This allows for:
* The storage of configurations within the `FS/home/sb` (The user name within the sandbox is obscured) 
* Passing of any file that may be too encumbering to provide via `--files` especially if the file doesn't actually exist at the path you want it to. For example:
	* Place a binary in `FS/usr/bin`
	* Place libraries in `FS/usr/lib`
	* Place files in `FS/tmp`.

In essence treat the `fs` folder as access to the root of the sandbox (Although not that files bound to the sandbox at runtime will not be there).

There are two values that can be passed to `--fs`:
* `--fs persist` will provide the folder Read+Write, and any changes made during program execution will persist.
* `--fs cache` will provide the folder, but won't save any changes made during program execution.

> [!warning]
> You will need to provide the home with `persist` at least once so that configuration files will actually be saved on disk. Once you have the program configured, and don't need any future changes to be saved, you can switch it to `cache`.

The switch also accepts a modifier, which changes the default path of `$XDG_DATA_HOME/sb/program/fs` to anything you want within that directory. This allows multiple silos of the app to exist at the same time. For example `--fs=persist:1` will use `$XDG_DATA_HOME/sb/program/1` instead.
## Namespaces + Networking

By default, the user namespace created by `bwrap` is completely separate from the host's namespaces, following the whitelisting approach mentioned prior. However, you may need some of these namespaces enabled, especially for networking.

The `--share (F)` switch controls the sharing of the following namespaces:
* `--share none` shares nothing, and is the default.
* `--share user` shares the user namespace, which is needed for Chromium/Electron to make their sandbox (This is automatically enabled with `--electron`)
* `--share ipc` shares the IPC namespace. 
* `--share pid` shares the PID namespace.
* `--share net` shares the Network namespace, and allows the program use the network.
* `--share cgroup` shares the CGroup namespace.
* `--share all` shares all namespaces.

>[!warning]
>You will likely only ever need to use `--share net` to give the sandbox internet access. `--share user` may be necessary if your program uses its own sandbox, but is already set with both `--electron` and `--gtk webkit`, so you likely do not need to ever enable it. Don't enable any of the other namespaces, **especially** `all`, unless you know what you're doing.

## SECCOMP

`bwrap` provides the feature to pass a BPF SECCOMP filter that will be enforced within the sandbox. This can strengthen the sandbox by reducing the syscalls that the program can use, and all its children. SB+SECCOMP+AppArmor can significantly reduce the risk of Zero Day attacks by preventing malicious exploits moving horizontally. SB reduces what files the program can see, SECCOMP reduces what syscalls it can use to act on those files, and AppArmor can mitigate sandbox escape by enforcing only the access of those files.

If you don't happen to know every single syscall your program ever uses, SB can fortunately automate the process:
1. Firstly, run the program with `--seccomp=strace`. This will run the program under `strace`, and setup a filter. This will not be able to exhaustively report all syscalls, however.
2. Secondly, use `sb-seccomp`, providing the `sb` script as the first argument, and the name of the local data folder of the program (It should be located within `$XDG_DATA_HOME/sb`. For example, `sb-seccomp chromium.desktop.sb chromium`, or `sb-seccomp org.kde.okular.desktop.sb okular`. This will require root access to read the `audit` log, which you'll also need installed and running, but will capture all used syscalls. Run this multiple times, using all the parts of the program you plan to use.
3. Finally, set `--seccomp=enforcing`. From this point on, only those syscalls captured will be allowed, with all other syscalls failing with `EPERM`. Most programs don't know how to handle a failed syscalls, and will just die. If your program randomly crashes, you might have missed a syscall, or it did something it wasn't supposed to.

> [!warning]
> If you're running an x86 computer, you may encounter an error that the `pread` or `pwrite` syscall is not recognized. Audit reports the syscall with those names, but they are actually architecture-specific, and hence you'll want to to modify the `$XDG_DATA_HOME/sb/program/syscalls.txt`, find those two names, and change them to `pread64` and `pwrite64` respectively

## Encryption

If your program contains sensitive information, you may want to secure it by encrypting it. The `--encrypt (F)` switch can encrypt the configuration and FS using `gocryptfs`. To setup:
1. Run the program with `--encrypt init`. This will create a new encrypted root for the program's configuration at `$XDG_DATA_HOME/sb/enc`. The cache for the program, and its FS will be encrypted within the root. If there's an existing configuration, it will be seamlessly copied. 
2. Provide the `--encrypt` switch to the profile. 

The `--encrypt persist` flag will make the encryption a one-time operation per boot. It will be locked on reboot, and on the first attempted execution will prompt for unlocking, then remain unlocked for the rest of the boot.

You'll need to install `gocryptfs`, along with `kdialog`, otherwise you will be only able to run the profile from the command line.

## Miscellaneous Switches

* `--dry (TF)`: Create the sandbox, but don't actually run the program.
* `--env (L)`: Pass `KEY=VALUE` environment variables to the sandbox.
* `--hardened-malloc (F)`: Enforce the use of `libhardened_malloc` within the sandbox, strengthening memory security at the cost of performance. You can either toggle the switch, or provide `--hardened-malloc light` to use the light variant.
* `--help (TF)`: Get information about all the switches.
* `--hostname (TF)` Provide the real host-name to the sandbox. You will likely not every need this.
* `--include (TF)`: Provide `/usr/include`, for C/C++ headers (IE, for IDEs)
* `--locale (TF)`: Provide the system locale to the sandbox. Most applications will default to the `C` locale, which may be all you need.
* `--no-flags (TF)`. By default, SB will source any additional arguments to pass to the *application* in a file located at `$XDG_DATA_HOME/sb/program/flags.txt`. This mimics the `chromium-flags.conf` file that Chromium/Electron uses. If you have the file, but have a drop-in version that shouldn't use it, provide this switch.
* `--post (C+M)`: Some applications, such as `yarr`, do not run as standalone programs, but instead act as services to which other programs, such as web browsers, interface with. `--post` can be used to run another program immediately after the sandboxed application, and to which the sandbox will close once the post application closes. The modifier can be used to pass arguments to the application, such as `--post chromium.desktop.sb:http://localhost:7070`
* `--python (C)`: Provide Python in the sandbox, specifying the version, such as `--python 3.13`
* `--shell (D)` Can be used two-fold:
	* `--shell=true` provides `/usr/bin/sh`, and creates a `/etc/passwd` file to make it the user's shell. This is needed for some programs, such as Zed, to resolve the environment.
	* `--shell=debug` does exactly what `--shell` does, but drops *into* that shell rather than launching the program, letting you look around the sandbox environment. This is very useful for debugging.
* `--spelling (F)` provides spellchecking support, including:
	* `--spelling hunspell` for Hunspell.
	* `--spelling enchant` for enchant.
* `--version (TF)` prints the SB version.




