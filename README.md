# SB

SB is a command line utility that sandboxes Linux applications via `bwrap`.  It wraps the bubble-wrap, simplifying the creation of sandboxes via:
1. Automatically resolving and managing library and binary dependencies via `ldd`.
2. Flatpak Portal emulation via `xdg-dbus-proxy` and `inotify`
3. Automatic SECCOMP filter generation via `libseccomp` and `strace`
4. Configurations for:
	1. QT 5/6 + KF6 Applications
	2. GTK 3/4 + WebKit Applications
	3. Electron/Chromium
	4. Networking
	5. Wayland and Xorg (Via `Xephyr`)
	6. Pipewire
5. Integration with Desktop Environments. Profiles are saved as script files in `~/.local/bin`, and with `--desktop-entry` will replace the original application, such that Desktop Environments will launch the sandboxed version automatically.
6. Automatic File Passthrough allowing for `.desktop` files to open default applications, such as through the file-explorer.
7. Profile Encryption via `gocryptfs`.

There are two implementations:

1. [Python-SB](./python/README.md) is the Python implementation. It is no longer maintained, but still fully functional.
2. [SB++](./cpp/README.md) is the C++ implementation. It has been written from the ground up and is faster, more functional, and easier to use. Its PGO build recipe and aggressive optimizations significantly speed up all operations. It's the recommended option.

## Why?

SB is designed to improve the security of Linux systems via:
1. *Isolation*: By separating applications from the host, SB increases the difficulty in chaining together exploits between different applications on the host. An application confined within SB would need to break out of the sandbox in order to attack other parts of the system.
2. *Reduced Attack Surface*: With dynamic dependency resolution and a whitelist approach to populating the sandbox, SB profiles contain only the bare minimum needed to run the application. SECCOMP Filters can reduce the syscall attack surface as well. For example, consider `cpp/examples/chromium.desktop.sb --stats`:

```
Reduced Total Syscalls By: 65.3731% (335 to 116)  
Reduced /dev By: 99.41% (678 to 4)  
Reduced /usr/bin By: 99.6241% (3192 to 12)  
Reduced /usr/lib By: 99.1509% (40750 to 346)  
Reduced Files By: 92.7379% (740617 to 53784)
```
