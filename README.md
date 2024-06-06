# SB

 Sandbox applications, securely.

## Installation

Download the PKGBUILD and run `makepkg -si`

## Setup

`sb` is a python script that wraps `bubblewrap`, providing Flatpak-like application isolation while still being able to use native applications with system libraries.

The main advantage to using `sb` is that it leverages `xdg-dbus-proxy` which allows it to filter D-Bus calls, and also allows the application to use XDG Desktop Portals for things like File Selection, which can simplify and compliment MAC like AppArmor.

## Portals

Simply running an application underneath bubblewrap will not cause it to start using portals to request files and display notifications. Most applications will only use these routes if it detects that it's running in a Flatpak environment, to which `sb` performs two actions:

1. For each running application, launch an `xdg-dbus-proxy` for that particular application, to which portals and buses can be specified depending on the application's particular use case.

2. Lie to both the application and the proxy, claiming that they're running under Flatpak.

Using the `--portals` argument, the traditional freedesktop portals of "Documents", "Flatpak", "Desktop", "Notifications", and "FileChooser" can be exposed to the application; "Flatpak" must be part of the list of passed portals for any of the others to work.

Additionally, any arbitrary bus can be specified through the `--see`, `--talk`, and `--own`. For example, to expose the `kwallet` bus, we can pass `--talk org.kde.kwalletd6` to `sb`.

## Bubblewrap Options

Most of `bubblewrap`'s native functionality also exists in `sb`, including:

* `--share` to share system namespaces such as `cgroup` or `net`. `--network` exists as a separate flag to ensure the application has relevant network files as well. Unlike `bwrap`, these are default unshared unless specifically told otherwise.
* `--capabilities` permits privileged users within the sandbox to perform elevated capabilities, such as `CAP_SYS_ADMIN` for chromium's `setuid` sandbox.
* `--sockets` can be used to passthrough the Wayland and Pipewire socket, granting graphics and sound respectively. No Xorg, sorry.
* `--enable-namespaces` allows for user namespace support in the sandbox, such as for chromium's non-`setuid` sandbox.

## File/Folder Mounts

`sb` supports mounting many of the top-level root folders, simplifying profile creation:

* `--dev` mounts `/dev` to the sandbox
* `--proc` mounts `/proc` to the sandbox
* `--etc` mounts `/efi` to the sandbox
* `--sys` mounts `/sys` to the sandbox
* `--usr-share` mounts `/usr/share` to the sandbox

Note that, by default, *none* of these folders are imported into the sandbox; running an application with `sb` without any arguments will lead to a comletely empty environment.

### Binaries and Libraries

Like the other root folders, both the `/bin` and `/usr/lib` (Which symlinks to `/lib`, `/lib64` and `/usr/lib64`) folders have arguments to mount: `--bin` and `--lib` respectively.

However, both of these file types offer more file-grained control with a `--binaries` and `--libraries` argument

#### Binaries

If `--bin` is not specified, rather than mounting the `/bin` folder into the sandbox, `sb` will place binaries specified in the `--binaries` argument into the environment; `sb` will automatically include the program itself.

While it may sound exceedingly difficult to determine what binaries are needed for an application--for most it's 0. For the profiles that I've created (More in the *Examples* section), the only times I've needed to explicitly include libraries if it the program is a actually a bash script.

You probably don't need to use either `--bin` or `--binaries`, the program itself is usually enough.

#### Libraries

If `--lib` is not specified, `sb` will utilize *SOF* to try and automatically determine the libraries that the application needs. It does this by recursively determining the needed libraries for the application itself, using `ldd`, and then performing that same depedency check on those dependencies, and so on and so forth. Any binary passed through `--binaries` will also be checked, `--libraries` can be used to provide files that were not found (Files, not folders)

When this tree is generated, we cannot just pass all of these files to `bwrap` directly, as it not only has an upper limit on arguments, but also slows the program down to a crawl. Additionally, this process is not instant, taking about ~10 seconds for larger programs like `chromium`, so `sb` *caches* the libraries.

By default, `sb` will store every library needed by any application it runs in `$XDG_DATA_HOME/sb/shared`. Then, for each application, hardlinks are made to its own individual folder; this ensures that at a *maximum*, the `shared` folder will never be larger than your `/lib` folder, which is usually only a couple of GB (Your file explorer may lie and say its bigger). Once the library has been cached, repeated launches of the application will be as quick as loading the `/lib` directory directly. However, since the files in `shared` are copied, you may need to periodically update the cache after an update. Use `--update-libraries` when calling `sb` to update the cache, or just delete the `sb` directory when you feel like it. If you have the RAM to spare, you can also call `sb` with `--sof-tmpfs` to place the cache in your `/tmp` directory, not only speeding up performance, but automatically cleaning the cache on rebote (With the downside being that you'll need to generate that cache on each subsequent startup)

With all that being said, unless you're awfully paranoid it's probably best to just pass your entire `/lib` folder into the sandbox; for a point of reference, this is the same thing AppArmor does when creating a new profile. *SOF* does not always capture every library that is needed, so if you do want to use it, be prepared to do some debugging to try and figure out what libraries are missing.

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

## Configuration Mounts

`sb` also has a few predefined switches for different applications, to ensure that needed libraries and files are within the sandbox:

* `--dri` will include various components needed for graphical applications, such as fonts, icons, themes, but also *dri* devices such as the graphics card to ensure hardware accelerated rendering. Pretty much every switch described here will pull this in, so you probably don't need to use it.
* `--qt` will include Qt5/6 files for running Qt applications
* `--kde` if a superset of `--qt`, and provides KDE related configuration files to make sure Qt applications are themed like the system.
* `--kf6` if a superset of `--kde`, and only applies if not using `--lib`. It pulls in KF6 and Kirigami libraries.
* `--gtk` provides files needed for running gtk3/4 applications.
* `--electron` provides files needed for running electron apps and chromium-based browsers. If running an electron app itself, you can additionally provide the `--electron-version` flag to pull a specific electron version, such as `electron28`.

## Desktop Entries and File Passthrough

Once a profile has been generated for a specific application, simply add the `--make-desktop-entry` to the command and `sb` will generate an executable file of the command located at `$HOME/.local/bin`, and create a desktop file pointing to that script. Launching the application from your desktop environment, the program will automatically get sandboxed by `sb`. Additionally, you can continue making modifications to the profile by simplying editing the file in your `bin` folder. If your application does not have the same name as your desktop profile, you may need to pass the `--desktop-entry` flag to specify the application, such as `zed.desktop`

Another feature of `sb` is that files that are passed, intended for the application, are automatically provided in the sandbox. This not only means that traditional command line arguments work, such as `chromium.desktop.sh https://duck.com`, but also means that you can pass files, such as `chromium.desktop.sh bookmarks.html`. Best of all, this automatically gets incorporated into the desktop file, so you can open any file that you application would usually support directly from your file explorer or other apps using something like `xdg-open`. By default, these files are exposed read-only, as they are passed directly rather than through a portal, but for applications that typically open things to edit them, such as text edtiors, you can pass `--file-passthrough-rw` to allow the application to make changes

## Debugging

Generating a profile for an application can take some time, here are some helpful tips if you're trying to minimize the size of your sandbox environment:

* The `--debug-shell` argument will drop you into `sh` rather than running your application, which you can use to navigate the sandbox and see what's missing (Note, unless you provided `--bin`, you're not going to have things like `ls` or `grep` to work with unless you explicitly add them with `--binaries`!)
* Use the `--binaries` flag to bring in helpful utilities into the sandbox environment, such as `strace`, where you can then run the application from within the environment and troubleshoot what's missing.
* Start with broad permission, such as `sb app --dev --proc --etc --sys --bin --lib`, and then start removing the permission to determine when functionality goes missing or the application fails. One thing I found particularly helpful was using `find` to provide files in batches after removing a generic keyword. If you remove `--usr-share` and the application fails, try passing `--ro $(find /usr/share -mindepth 1 -maxdepth 1 -name "[a,b,c,d]\*")` to see if that subset of the folder works.

## Examples

With everything said, it may seem daunting to actually create a profile, especially for larger applications--but it's actually quite easy. All of the below examples use minimal permissions (No `--bin` or `--lib`), and after initial caching are close to native performance:

### Chromium

```bash
sb chromium "$@" --portals Flatpak Desktop Notifications FileChooser --sockets wayland pipewire --kde --electron --app-dirs cache config etc share lib --rw $XDG_CONFIG_HOME/chromium-flags.conf --enable-namespaces --talk org.kde.kwalletd6 --own org.mpris.MediaPlayer2.chromium.instance2 --share net --devices /dev/video0 --binaries /usr/lib/chromium/chromium
```

This profile runs `chromium` underneath Wayland, with Pipewire for sound, while passing through `kwallet` communication and a webcam at `/dev/video`. `--capabilities` is not particularly supported, so while it would probably be possible to get chromium to use the setuid sandbox, passing `--enable-namespaces` allows for the newer version of browser sandboxing to be used within the application sandbox. `--kde` is passed to get theming options passed to the browser, and `FileChooser` is allowed for Portal access for Downloading/Uploading. Because we aren't using `--lib`, we pass the main `chromium` executable at `/usr/lib/chromium/chromium` as a binary (It would've been included already at `--app-dirs lib`) so that dependencies can be correctly determined.

### Obsidian

```bash
sb obsidian "$@" --portals Documents Flatpak Desktop FileChooser --sockets wayland --kde --electron --electron-version 28 --app-dirs config lib --enable-namespaces --binaries bash grep
```

Electron applications are similar to chromium itself, save the need for a specific electron version. This bundles the library and executable into the sandbox. Also, since my `/usr/bin/obsidian` isn't actually an executable, but a shell script, we need to include `bash` and `grep` to properly run it.

### Okular

```bash
sb okular "$@" --portals Flatpak Desktop FileChooser --sockets wayland --kf6 --app-dirs data share --rw $XDG_CONFIG_HOME/okularpartrc $XDG_CONFIG_HOME/okularrc --libraries /usr/lib/libOkular6Core.so{,.1,.1.0.0}
```

Qt applications can generally be run with the `--qt` flag, but missing KF6 and Kirigami can make them look ugly, hence the `--kf6` flag. Additionally, we need to pull in some configuration files, and also some libraries that *SOF* didn't catch.

### Zed

```bash
sb Zed "$@" --portals Documents Flatpak Desktop FileChooser --share net --rw $XDG_DATA_HOME/zed $XDG_CACHE_HOME/zed $XDG_CONFIG_HOME/zed --dri --file-passthrough-rw --sockets wayland --locale
```

Despite being named *Zed*, all of its files are named in lowercase *zed*, which makes it incredibly frustrating that none of the `--app-dir` folders work on it. However, because this application uses neither Qt or GTK, we only pass `--dri`, and have to provide the application locale information, otherwise it crashed beacause `/usr/share/X11/locale` is missing. To make a desktop entry, since we need to use `--desktop-entry` for this case:

```bash
sb Zed "$@" --portals Documents Flatpak Desktop FileChooser --share net --rw $XDG_DATA_HOME/zed $XDG_CACHE_HOME/zed $XDG_CONFIG_HOME/zed --dri --file-passthrough-rw --sockets wayland --locale --make-desktop-entry --desktop-entry zed.desktop
```

### QMMP

```bash
sb qmmp "$@" --portals Flatpak Desktop Notifications FileChooser --sockets wayland pipewire --kf6 --app-dirs share config data --ro /usr/lib/qmmp-2.1
```

Finally, a simple configuration, only requiring a manual library update. Note that `--libraries` only takes *files*, so if you want to pass a folder, do so in the `--ro` argument. While this application isn't technically part of KF6, you can try just passing `--qt` or `--kde` instead, realize how awful it looks, and then change it back. However, since this profile is using *SOF*, the libraries will be cached, and you won't actually update the libraries. Here, we can just call it with `--update-libraries`:

```bash
sb qmmp "$@" --portals Flatpak Desktop Notifications FileChooser --sockets wayland pipewire --kf6 --app-dirs share config data --ro /usr/lib/qmmp-2.1 --update-libraries
```

And it will add the needed libraries to the sandbox. Note while this method can *add* libraries, it cannot *remove* them. If you're experimenting around, it's better to just delete the folder at `$XDG_DATA_HOME/sb/app` instead (Or just throw onto the tmpfs)
