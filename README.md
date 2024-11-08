# SB

 Sandbox applications, securely.
 
 | Idle Memory Usage | SOF on TMPFS | SOF on ZRAM | Actual Size |
 | ----------------- | ------------ | ----------- | ----------- |
 | 5.8 GB            | 7.6 GB       | 6.4 GB      | 1.8 GB      |
 

## Installation

Download the PKGBUILD and run `makepkg -si`. On non-pacman based distributions, `make` will compile the `sb` binary. which can be executed on any system with Python 3.

## Setup

`sb` is a python script that wraps `bubblewrap`, providing Flatpak-like application isolation while still being able to use native applications with system libraries.

The main advantage to using `sb` is that it leverages `xdg-dbus-proxy` which allows it to filter D-Bus calls, and also allows the application to use XDG Desktop Portals for things like File Selection, which can simplify and compliment MAC like AppArmor. Another advantage is performance, as `sb` runs in memory; when using `--cached-home`, the entire application, from binaries, shared libraries, and configuration files are stored on RAM to drastically improve speed, segregate the application from the system, while having a minimal memory footprint.

## Portals

Simply running an application underneath bubblewrap will not cause it to start using portals to request files and display notifications. Most applications will only use these routes if it detects that it's running in a Flatpak environment, to which `sb` performs two actions:

1. For each running application, launch an `xdg-dbus-proxy` for that particular application, to which portals and buses can be specified depending on the application's particular use case.

2. Lie to both the application and the proxy, claiming that they're running under Flatpak.

Using the `--portals` argument, any bus exposed at the `org.freedesktop.portal` can be passed to the application; "Flatpak" must be part of the list of passed portals for any of the others to work. Common choices include:

1. `FileChooser`: To give the application a DE-agnostic file picker to pass files into the sandbox without explicitly passing them through via `sb`.

2. `Documents`: To give the application a folder on a more permanent lease than `FileChooser`, mounted at `/run/user/$UID/doc`.

3. `Desktop`: For access to various DE integrations, such as themes and fonts.

4. `Camera`: For more stable access to the system webcam, unlike `/dev/video`

5. `OpenURI`: The modern version of `xdg-open`.

Additionally, any arbitrary bus can be specified through the `--see`, `--talk`, and `--own`. For example, to expose the `kwallet` bus, we can pass `--talk org.kde.kwalletd6` to `sb`. `--see` allows the application to read values from the bus, `--talk` allows it to read/write values, and `--own` is for busses specific to that application, such as `mpris` busses for media controls.

## Bubblewrap Options

Most of `bubblewrap`'s native functionality also exists in `sb`, including:

* `--share` to share system namespaces such as `cgroup` or `net`. `--share net` will pass additional network configurations so that networking will work correctly. Unlike `bwrap`, these are default unshared unless specifically told otherwise.
* `--sockets` can be used to passthrough the Wayland and Pipewire socket, granting graphics and sound respectively.

> [!warning]
> `--sockets xorg` is provided for compatibility purposes, but presents a means of sandbox escape. Use Wayland.

## File/Folder Mounts

`sb` supports mounting many of the top-level root folders, simplifying profile creation:

* `--dev` mounts `/dev` to the sandbox
* `--proc` mounts `/proc` to the sandbox
* `--etc` mounts `/efi` to the sandbox
* `--sys` mounts `/sys` to the sandbox
* `--usr-share` mounts `/usr/share` to the sandbox

> [!warning]
> None of these folders are imported into the sandbox by default. `sb` operates on the principle of whitelisting, where the default value is the most restricted.

### Binaries and Libraries

Like the other root folders, both the `/bin` and `/usr/lib` (Which symlinks to `/lib`, `/lib64` and `/usr/lib64`) folders have arguments to mount: `--bin` and `--lib` respectively.

However, both of these file types offer more file-grained control with a `--binaries` and `--libraries` argument.

> [!warning]
> `--bin` and `--lib` exist mostly to create a broad sandbox that can then be refined; `sb` has intelligent means of determining binaries and libraries that are needed, which should be used in conjunction with `--app-dirs` and explicit `--binaries` and `--libraries` to create a minimal footprint. `--bin` will give the sandbox every binary on the system, and `--lib` will give every library.


#### Binaries

If `--bin` is not specified, rather than mounting the `/bin` folder into the sandbox, `sb` will place binaries specified in the `--binaries` argument into the environment; `sb` will automatically include the program itself. Additionally, `sb` will automatically scan script files to not only ensure the correct interpreter is available from the shebang, but also that commands used within the script are also available. You should not need to explicitly pass shells into the sandbox. Binaries are mounted read-only.

> [!info]
> `sb` caches the required binaries from shell scripts in `$XDG_DATA_HOME/sb/cache`. These can be updated with either `sb-refresh`, or by passing `--update-cache` to any `sb` program.


#### Libraries and Caches

If `--lib` is not specified, `sb` will utilize *SOF* to try and automatically determine the libraries that the application needs. It does this by recursively determining the needed libraries for the application itself, using `ldd`, and then performing that same dependency check on those dependencies, and so on and so forth. Any binary passed through `--binaries` will also be checked, `--libraries` can be used to provide files that were not found. `--libraries` accepts files, folders, and wildcard patterns. Libraries are mounted read-only.

When this tree is generated, we cannot just pass all of these files to `bwrap` directly, as it not only has an upper limit on arguments, but also slows the program down to a crawl. Additionally, this process is not instant, taking about ~10 seconds for larger programs like `chromium`, so `sb` *caches* the libraries. Each application will have a local folder located at `$XDG_DATA_HOME/sb/$APP`, which will contain a `lib.cache` containing every library needed for the application. When the application is run, it will read this file, and create a temporary `/usr/lib` for the application at `/tmp/lib/$APP`, shared between all instances as a read-only bind.

You can use the `--update-libraries` argument to regenerate this list.

> [!info]
> The `/tmp/sb` folder employs hard-links to share libraries copied into the TMPFS between all running `sb` programs. This means that, at most, `sb` will consume the size of `/usr/lib` in memory, but in most cases this value is much lower; if your system has limited memory, you may be unable to use `sb`.

`sb` will also compute a command cache, which translates the `sb` call into a `bwrap` call, stored with a hash of the former. Once you've created a `sb` configuration that works, `sb` will simply use the cached `bwrap` such that launching an application is much faster.

There is a slight delay on cold-start, as shared libraries must be copied to the TMPFS, but you can avoid this by creating a desktop entry (More below), and providing the `--dry-startup` flag in the binary at `~/.local/bin/$APP.sb`, and then enabling `sb.service`. This will dry run the application on startup, populating the TMPFS before use.

> [!info]
> Combined together, the library cache, command cache, and `--dry-startup` flags led to startup speed comparable to, or even *faster* than native application startup due to the data being cached in RAM.

> [!warning]
> Using `--dry-startup` has diminishing returns, but because libraries on the TMPFS are shared between all programs, you only need to dry startup a few app "categories" to get the benefits. For example, if you dry startup `kate`, all KF6/QT6 applications will have their shared libraries cached, so `okular` will launch far quicker.


### Arbitrary Files and Application Specific Files

`sb` supports two arguments for providing an arbitrary file, which must be provided as an absolute path:

* `--rw`: A list of files that can be modified by the program within the sandbox.
* `--ro`: A list of files that are in the sandbox, but can't be modified.

Importantly, these files are added at the end of the sandbox construction, which makes them ideal for adding files in directories that have already been mounted by the previous folder mounts.

Additionally, `sb` has an `--app-dirs` folder, which specifies folders that the application owns located in various directories. Chromium, for example:
1. `config`: `$XDG_CONFIG_HOME/chromium`
2. `cache`: `$XDG_CACHE_HOME/chromium`
3. `etc`: `/etc/chromium`
4. `share`: `/usr/share/chromium`
5. `data`: `$XDG_DATA_HOME/chromium`
6. `lib`: `/usr/lib/chromium`  (Mounted RO)

If you would instead like to isolate the application from your home directory, you can pass the `--home` argument to create a unique home directory for each application in the *SOF* folder, keeping your home folder clean.

## Configuration Mounts

`sb` also has a few predefined switches for different applications, to ensure that needed libraries and files are within the sandbox:

* `--dri` will include various components needed for graphical applications, such as fonts, icons, themes, but also *dri* devices such as the graphics card to ensure hardware accelerated rendering. Pretty much every switch described here will pull this in, so you probably don't need to use it.
* `--qt` will include Qt6 files for running Qt applications. Use `--qt5` if your application hasn't been updated.
* `--kde` if a superset of `--qt`, and provides KDE related configuration files to make sure Qt applications are themed like the system.
* `--gtk` provides files needed for running gtk3/4 applications.
* `--electron` provides files needed for running electron apps and chromium-based browsers. If running an electron app itself, you can additionally provide the `--electron-version` flag to pull a specific electron version, such as `electron28`.

## Desktop Entries and File Passthrough

Once a profile has been generated for a specific application, simply add the `--make-desktop-entry` to the command and `sb` will generate an executable file of the command located at `$HOME/.local/bin`, and create a desktop file pointing to that script. Launching the application from your desktop environment, the program will automatically get sandboxed by `sb`. Additionally, you can continue making modifications to the profile by simplying editing the file in your `bin` folder. If your application does not have the same name as your desktop profile, you may need to pass the `--desktop-entry` flag to specify the application, such as `dev.zed.Zed.desktop`

Another feature of `sb` is that files that are passed, intended for the application, are automatically provided in the sandbox. This not only means that traditional command line arguments work, such as `chromium.desktop.sh https://duck.com`, but also means that you can pass files, such as `chromium.desktop.sh bookmarks.html`. Best of all, this automatically gets incorporated into the desktop file, so you can open any file that you application would usually support directly from your file explorer or other apps using something like `xdg-open`. By default, these files are exposed read-only, as they are passed directly rather than through a portal, but for applications that typically open things to edit them, such as text editors, you can pass `--file-passthrough-rw` to allow the application to make changes.

If you run into issues with Read-Write passthrough, it may because of how your application handles writes. Because `bubblewrap` exposes these files via bind mounts, if your application does not write to the file directly, but instead makes a copy and then overwrites it, it will be unable to overwrite the mount, and fail. To solve this, use the `--file-enclave` option, which will copy the file to the sandbox, and then overwrite the contents when the application closes.

> [!warning]
> The file enclave does not update the original file contents until *after* the application has closed. This means that, for RW-encalves, if `sb` or the system crashes, the changes will not be updated back to disk, and any changes you've made to that file in another application will be overwritten when the sandbox closes.

## Debugging

Generating a profile for an application can take some time, here are some helpful tips if you're trying to minimize the size of your sandbox environment:

* The `--debug-shell` argument will drop you into `sh` rather than running your application, which you can use to navigate the sandbox and see what's missing (Note, unless you provided `--bin`, you're not going to have things like `ls` or `grep` to work with unless you explicitly add them with `--binaries`!)
* Use the `--binaries` flag to bring in helpful utilities into the sandbox environment, such as `strace`, where you can then run the application from within the environment and troubleshoot what's missing.
* Start with broad permission, such as `sb app --dev --proc --etc --sys --bin --lib`, and then start removing the permission to determine when functionality goes missing or the application fails. One thing I found particularly helpful was using `find` to provide files in batches after removing a generic keyword. If you remove `--usr-share` and the application fails, try passing `--ro $(find /usr/share -mindepth 1 -maxdepth 1 -name "[a,b,c,d]\*")` to see if that subset of the folder works.

## Examples

With everything said, it may seem daunting to actually create a profile, especially for larger applications--but it's actually quite easy. All of the below examples use minimal permissions (No `--bin` or `--lib`), and after initial caching are close to native performance:

### Chromium

```bash
sb chromium "$@" --portals Flatpak Desktop Notifications FileChooser Camera OpenURI --sockets wayland pipewire xorg --electron --app-dirs etc share lib --zypak --own org.mpris.MediaPlayer2.chromium.instance2 --libraries /usr/lib/chromium/chromium --home --share net --file-passthrough-rw --dry-startup --cached-home
```

This profile runs `chromium` underneath Wayland, with Pipewire for sound. Xorg passthrough is due to an issue with VA-API on AMD. `FileChooser` is allowed for Portal access for Downloading/Uploading. We give a custom home directory, networking, allow it to make modifications to passthrough files, like PDFs, and dry-startup the browser so there's no delay after a reboot. `--cached-home` copies the custom home to TMPFS for each instance, ensuring a clean slate each time.

> [!tip]
> To find busses the application uses, run `sb` with the `--verbose` flag.
> Calls to the `dbus-proxy` will be written to console, so you can find
> Calls to busses like `org.mpris.MediaPlayer2.chromium.instance2`!

> [!tip]
> Chromium application, such as Electron, require user namespaces when not using the deprecated setuid sandbox. `sb` does not support the latter, and the former has been deprecated due to major security issues. To remedy this, you can use `zypak` to translate sandboxing calls from chromium to `sb`, ensuring chromium can use its own sandbox within `sb` without needing user namespaces or the dreaded `--no-sandbox` flag.

### Obsidian

```bash
sb obsidian "$@" --portals Documents Flatpak Desktop --sockets wayland pipewire --electron --electron-version 30 --app-dirs lib --home --zypak --share net
```

Electron applications are similar to chromium itself, save the need for a specific electron version if you using a system electron instead of a bundled one

### Okular

```bash
sb okular "$@" --portals Flatpak Desktop FileChooser --sockets wayland --kde --app-dirs share --home --libraries "libOkular6Core*" "libpoppler-qt*" --own org.kde.okular-2 --file-enclave
```

Qt applications can generally be run with the `--qt` flag, but missing KF6 and Kirigami can make them look ugly, hence the `--kde` flag. Additionally, we need to pull in some configuration files, and also some libraries that *SOF* didn't catch. Arbitrary file passthrough can often lead to issues with other layers of security, such as AppArmor, so rather than permitting Okular access to all home files, and use `sb` to mediate which files it gets, we can use `--file-enclave` and just give okular access to the `/enclave` folder in the sandbox; since we aren't giving it RW access, there isn't a concern of overwriting or data loss.

### Zed

```bash
sb /usr/lib/zed/zed-editor "$@" --portals Documents Flatpak Desktop FileChooser --dri --home --sockets wayland --file-passthrough-rw --devices /dev/null --binaries typos typos-lsp env clangd ruff ruff-lsp markdown-oxide --dev --dry-startup --xdg-open --zsh --include --cached-home
```

Because this application uses neither Qt or GTK, we only pass `--dri` To make a desktop entry, since we need to use `--desktop-entry` for this case:

```bash
sb /usr/lib/zed/zed-editor ... --make-desktop-entry --desktop-entry dev.zed.Zed.desktop
```

This will create a `dev.zed.Zed.desktop.sb` in ~/.local/bin`

We provide a bunch of LSP binaries so that Zed can use them in the sandbox, alongside the ZSH shell and system include headers for clang. `--dev` is usually a discouraged switch because it gives blanket access to the `/dev` folder, but here it's necessary as Zed is unable to render the integrated terminal without it.

### qBittorrent

```bash
/usr/bin/sb qbittorrent "$@" --portals Documents Flatpak Desktop FileChooser --sockets wayland --kde --home --share net
```

A simple configuration. You could use `--qt`, but then `qbittorrent` wouldn't look very nice.

### Fooyin

```bash
sb fooyin "$@" --portals Documents Flatpak Desktop Notifications FileChooser --sockets wayland pipewire --kde --home --proc --own org.mpris.MediaPlayer2.fooyin --lib --share-cache
```

Another relatively simple configuration. We pass PipeWire and the MPRIS handle for fooyin so we can play music and control it from our DE. `--lib` is another unfortunate switch, but was necessary for some reason. Finally, `--share-cache` ensures that when Fooyin puts thumbnails in `~/.cache`, those images are actually available to the DE to display, rather than being in its own namespace and thus inaccessible.
