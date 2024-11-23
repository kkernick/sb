# SB

```
usage: sb [-h] [--portals [PORTALS ...]] [--see [SEE ...]] [--talk [TALK ...]] [--own [OWN ...]]
[--share [{user,ipc,pid,net,uts,cgroup,all,none} ...]] [--sockets [{wayland,pipewire,xorg} ...]] [--bin]
[--binaries [BINARIES ...]] [--lib] [--libraries [LIBRARIES ...]] [--local [LOCAL ...]] [--sof {tmpfs,data,zram}]
[--ignore [IGNORE ...]] [--update-libraries] [--update-cache] [--env [ENV ...]] [--rw [RW ...]] [--ro [RO ...]]
[--app-dirs [{config,cache,etc,share,data,lib} ...]] [--dev] [--devices [DEVICES ...]] [--proc] [--etc] [--sys] [--usr-share]
[--dri] [--qt] [--qt5] [--electron] [--electron-version ELECTRON_VERSION] [--python PYTHON] [--kde] [--gtk] [--zsh]
[--include] [--home] [--cached-home] [--share-cache] [--file-passthrough {off,ro,rw,writeback}] [--files [FILES ...]]
[--debug-shell] [--strace] [--xdg-open] [--real-hostname] [--locale] [--hunspell] [--git] [--hardened-malloc]
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

There are also optional dependencies that allow the use of various switches:
1. `strace`: for --strace, useful for debugging
2. `hardened_malloc`: for --hardened-malloc, highly recommended
3. `zram-generator`: for --sof=zram, highly recommended
4. `xdg-dbus-proxy`: for --portals, --see, --talk, --own for Flatpak Emulation, highly recommended.
