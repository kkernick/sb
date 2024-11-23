# Design

`sb` is a Python application that wraps the `bubblewrap` sandboxing tool as a means to simplify sandbox creation, and extend `bubblewrap`'s functionality.

## Security

As a sandboxing application, `sb`'s priority lies in security. This is done in two primary ways:
1. By intelligently determining what files are necessary to include, creating a minimal sandbox with minimal attack surface.
2. By leverging `bubblewrap` and other technologies to strengthen security within the sandbox, and maintain strong separation between the host.

The first point is achieved via SOF, which intelligently scans an application using a combination of `ldd` and manual parsing to determine all required shared libraries and binaries for an application. Shared libraries are recursively searched, and shell scripts are analyzed to find commands used. `sb` then creates a `lib.cache` file that contains all of these dependencies, which can then be immediately loaded into the sandbox after initial cache generation.

For the second point, `sb` uses `bubblewrap` very conservatively. It passes the `--clearenv` flag and by default disables all namespaces; this includes the network namespace to provide networking capabilities, and the user namespace which provides the ability to run sub-user-namespaces within the sandbox. Sandboxes can be configured to use `libhardened-malloc`, files passed through are exposed in the `/enclave` directory, complementing AppArmor profiles, and sandboxes are never given access to the home directory, instead either given a tempoary one for each program instance, or a static, separate home specific to that app stored in `$XDG_DATA_HOME/sb` with the `--home` switch.

`sb` also subscribes to a whitelisting approach, where by default the sandbox contains nothing, and it is the onus of the user to explicitly define features that are needed. For example, by default no devices are shared, a `/proc` is not mounted, a `/home` directory does not exist and the application runs under `nobody`, and all environment variables from the host are cleared in the sandbox. Fortunately, `sb` has numerous flags for common use cases, such as the `--dri` switch, which provides access to the render device and common files needed for running graphical applications, such as font configuration, themes, and color schemes. See USAGE for a more exhaustive explanation of available options.

For example, consider the following table that shows files at important locations within the sandbox, compared to how many actually exist on the host:

| Path | Host  | Chromium | Zed  | Okular | Tor Browser |
| ---- | ----- | -------- | ---- | ------ | ----------- |
| /dev | 299   | 12       | 11   | 3      | 5           |
| /etc | 1163  | 185      | 8    | 4      | 184         |
| /lib | 37406 | 423      | 1234 | 5686   | 5491        |
| /bin | 2402  | 1        | 10   | 1      | 34          |

> [!note]
> `/lib` and `/bin` are symlinked to `/usr/lib` and `/usr/lib` respectively within the sandbox, as are `/lib64`, `/usr/lib64`, and `/sbin`.

> [!note]
> The Zed sandbox included the LSPs `markdown-oxide`, `typos`, `clangd`, and `ruff`, alongside full support for `zsh` and system headers.

## Portals

`sb` leverages `xdg-dbus-proxy` to effectively lie to the application that it is running within Flatpak. When operating under this mode, applications will use XDG Portals to communicate, allowing us to leverage the FileChooser portal to mediate access to files on the host, Documents to give the application more permanent access to folders and projects, OpenURI to open links and files in the default application, and more. By default, no portals are allowed, an instance of `xdg-dbus-proxy` is not run, and the application runs under `nobody`.

However, if you want the application access to the current Desktop Theme, or ability to open files outside the sandbox, you will need to enables these options through a combination of the `--portals`, `--see`, `--talk`, and `--own` switches. Due to security restrictions of DBus, the application will also need to run under the users UID to access the session bus (Although the sandbox still uses a dummy `sb` username instead of the actual users, so long as as `/etc/passwd` isn't passed to the sandbox). Using Portals is an excellent way to provide an application with files, without having to explicitly passthrough entire folders; instead of passing through the user's Documents folder, they can instead pass through select files through the FileChooser portal when they are needed. This massively reduces access to the filesystem.

Portals also mesh excellently with the File Enclave, which allows for sandboxed applications to open files, in that they work with MAC systems, particularly AppArmor as it uses file paths rather than labels. Rather than granting the application wide sweeping permission, such as `@{HOME}/Documents rw`, we can instead just provide `/{enclave,run/user/*/doc} rw`, which once again massively reduces attack surface. It is *strongly* recommended to use `sb` alongside AppArmor for defense in depth.

## Privacy

As mentioned in the previous sections, `sb`'s restriction on files, `bubblewrap` configuration, and use of Portals all aid in reducing private information within the sandbox. So long as `/etc/passwd` is not passed into the sandbox, the application will only be presented with a `sb` user, there will be no environment variables containing useful information, and all private user files will be temporarily mediated through portals. If portals are not required, this abstraction is further strengthened by running the application as `nobody` and denying any access to the User Session.

## Performance

In contrast to the most popular use of `bubblewrap`, Flatpak, `sb` uses the system's libraries directly. However, rather than exposing these libraries directly, such as binding `/usr/lib`, `sb` uses SOF to intelligently determine library dependencies; however, this imposes two performance considerations: the time needed to generate that list of dependencies, and the creation of a temporary library folder for the application. Prolific caching is employed to reduce the latency for both operations, and TMPFS is used to reduce disk usage with redundant libraries.

### Caching

For each application, `sb` creates a `lib.cache` containing all needed libraries, but also a `cmd.cache` that contains a `bubblewrap` command alongside a hash of the `sb` command used to generate it. Additionally, folders in `/usr/lib` are not searched, but instead overlain onto the SOF (Particularly useful for the colosal QT folders), with a cache in `$XDG_DATA_HOME/sb/cache` containing libraries that those directories need. Likewise, shell script dependencies are cached. This considerably reduces cold startup, and hot startup is almost identical to running the application natively.

### TMPFS

`sb`, by default, creates the SOF directory in `/tmp`. This has two impliciations: firstly, the user's disk will not be bloated with libraries, as the SOF is stored on RAM. Secondly, the SOF is ephemeral, so the user does not need to worry about cached definitions becoming outdated after system updates.

To reduce memory usage, `sb` employs hardlinks such that if two applications use the same library, only a single file will actually be created in RAM. This ensures that the `sb` SOF folders will never exceed the size of `/usr/lib`, but in practice it never exceeds more than a few GB.

To improve both performance and memory usage, `sb` can use `zram-generator` to store the `SOF` on a zstd-compressed ramdisk. Below is a table outlining how substantial this improvement can be:

| Idle Memory Usage | SOF on TMPFS | SOF on ZRAM | Actual Size | Compressed |
| ----------------- | ------------ | ----------- | ----------- | ---------- |
| 5.8 GB            | 7.6 GB       | 6.4 GB      | 1.8 GB      | 0.6 GB     |

To enable, simply install `zram-generator`, copy `sb.conf` to the relevant location for your distribution (Such as `/etc/systemd/zram-generator.conf.d/`), and use the `--sof=zram` flag.

### Dry Startup

Finally, `sb` includes a startup service, `sb.service`, that can be optionally enabled on user startup. When run, it iterates through all profiles located in `~/.local/bin`, and dry-runs those with the `--dry-startup` flag. This populates the SOF directories if they are stored on RAM, removing the cold-startup performance penalty entirely for select applications.
