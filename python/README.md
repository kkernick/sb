# SB

```
usage: sb [-h] [--portals [PORTALS ...]] [--see [SEE ...]] [--talk [TALK ...]] [--own [OWN ...]] [--no-flatpak]
[--share [{user,ipc,pid,net,uts,cgroup,all,none} ...]] [--sockets [{wayland,pipewire} ...]] [--syscalls [SYSCALLS ...]]
[--seccomp-log] [--seccomp-group] [--command COMMAND] [--args [ARGS ...]] [--post-command POST_COMMAND]
[--post-args [POST_ARGS ...]] [--bin] [--binaries [BINARIES ...]] [--xdg-open] [--bwrap] [--lib]
[--libraries [LIBRARIES ...]] [--sof {tmpfs,data,zram}] [--update-libraries] [--update-cache] [--env [ENV ...]]
[--rw [RW ...]] [--ro [RO ...]] [--app-dirs [{config,cache,etc,share,data,lib} ...]] [--dev] [--devices [DEVICES ...]]
[--proc] [--dri] [--fonts [FONTS ...]] [--qt] [--qt5] [--electron] [--electron-version ELECTRON_VERSION] [--python PYTHON]
[--kde] [--gtk] [--gst] [--webkit] [--shell] [--include] [--fs {none,persist,cache}] [--fs-location FS_LOCATION] [--no-tmpfs]
[--file-passthrough {off,ro,rw}] [--files [FILES ...]] [--debug-shell] [--strace] [--locale] [--spelling] [--hardened-malloc]
[--make-desktop-entry] [--desktop-entry DESKTOP_ENTRY] [--make-script] [--verbose] [--dry] [--startup] [--dry-startup]
program
```

`sb` is a Python Application that sandboxes applications using `bubblewrap` with minimal configuration. The primary motivation of the project is to create minimal sandboxes that only contain what an application needs to function.

See the `docs` folder for documentation and the `examples` folder for examples.

## Building

Simply run `make` to compile the `sb` binary; due to the scale of the project, the source code has been broken into separate files that are compiled into a self-executing zip file that Python natively supports. You will need the `zip` buildtime dependency.

Additionally, `sb` has the following runtime dependencies that you will need installed to function correctly:
1. `python3`
2. `findutils`
3. `glibc`
4. `which`
5. `bubblewrap>=0.11`
6. `python-inotify-simple`

There are also optional dependencies that allow the use of various switches:
1. `strace`: for `--strace`, useful for debugging
2. `hardened_malloc`: for `--hardened-malloc`, highly recommended
3. `zram-generator`: for `--sof=zram`, highly recommended
4. `xdg-dbus-proxy`: for `--portals`, `--see`, `--talk`, `--own` for Flatpak Emulation, highly recommended.
5. `python-libseccomp`: for `--syscalls`. Recommended; you can use `sb-seccomp` to generate the needed syscalls.

## Scripts and Services

* `sb.conf` is a zram configuration. If you have `zram-generator` installed and want `sb` to use zram as an SOF backing, copy it to `/usr/lib/systemd/zram-generator.conf.d`
* `sb.service` is a user systemd service that dry runs applications tagged with `--dry-startup`. To use, copy it to `/usr/lib/systemd/user/`, copy `sb-startup` to somewhere in your PATH, and then run `systemctl daemon-reload --user` followed by `systemctl enable sb --user`.
* `sb-refresh` will refresh all caches. Use it when upgrading `sb`, or when you feel like it.
