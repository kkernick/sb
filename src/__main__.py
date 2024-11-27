#!/bin/python

from os import environ
from subprocess import Popen, run
from time import sleep
from pathlib import Path
from tempfile import TemporaryDirectory, NamedTemporaryFile
from hashlib import new

from shared import args, output, log, share, cache, config, data, home, runtime, session, nobody, real, env
from util import desktop_entry

import binaries
import libraries

temp_dir = "/run/sb/temp" if args["sof"] == "zram" else "/tmp"
Path(temp_dir).mkdir(exist_ok=True, parents=True)


def main():
    # If we are making a desktop entry, or on startup and it's not a startup app, do the action and return.
    if args["make_desktop_entry"] or args["make_script"]:
        desktop_entry()
    if args["startup"] and not args["dry_startup"]:
        return

    # Get the basename of the program.
    path = args["program"]
    if path.startswith("/"):
        program = path.split("/")[-1]
    else:
        program = path

    temp = TemporaryDirectory(prefix=program + "-", suffix="-temp", dir=temp_dir)
    if not args["portals"] and not args["see"] and not args["talk"] and not args["own"]:
        log("No portals requiried, disabling dbus-proxy")
        application_folder = None
        info = Path()
    else:
        log("Creating dbus proxy...")

        # Setup the .flatpak info values.
        application_name=f"app.application.{program}"

        # Create the application's runtime folder
        application_folder=Path(runtime, "app", application_name)
        application_folder.mkdir(parents=True, exist_ok=True)

        # The .flatpak-info is just a temporary file, but the application's temporary directory is a separate TMPFS,
        # separate from the global /tmp.
        info = NamedTemporaryFile(prefix=program + "-", suffix="-flatpak-info", dir=temp_dir)

        log(f"Running {application_name}")
        log(f"\tTemp: {temp}")

        # Write the .flatpak-info file so the application thinks its running in flatpak and thus portals work.
        info.write(b"[Application]\n")
        info.write(f"name={application_name}\n".encode())
        info.write(b"[Instance]\n")
        info.write(f"app-path={path}\n".encode())
        info.flush()

        # Setup the DBux Proxy. This is needed to mediate Dbus calls, and enable Portals.
        log("Launching D-Bus Proxy")
        dbus_proxy(args["portals"], application_folder, info.name)

        # We need to wait a slight amount of time for the Proxy to get up and running.
        sleep(0.01)

    # Then, run the program.
    log("Launching ", program, "at", path)
    run_application(program, path, application_folder, info.name, temp.name)


# Run the DBUS Proxy.
def dbus_proxy(portals, application_folder, info_name):
    command = ["bwrap"]
    if args["hardened_malloc"]:
        command.extend(["--setenv", "LD_PRELOAD", "/usr/lib/libhardened_malloc.so"])
    command.extend([
        "--new-session",
        "--symlink", "/usr/lib64", "/lib64",
        "--ro-bind", "/usr/lib", "/usr/lib",
        "--ro-bind", "/usr/lib64", "/usr/lib64",
        "--ro-bind", "/usr/bin", "/usr/bin",
        "--clearenv",
        "--disable-userns",
        "--assert-userns-disabled",
        "--unshare-all",
        "--unshare-user",
        "--bind", runtime, runtime,
        "--ro-bind", info_name, "/.flatpak-info",
        "--die-with-parent",
        "--",
        "xdg-dbus-proxy",
        environ["DBUS_SESSION_BUS_ADDRESS"],
        f"{application_folder}/bus",
        "--filter",
        '--call=org.freedesktop.portal.Desktop=org.freedesktop.portal.Settings.Read@/org/freedesktop/portal/desktop',
        '--broadcast=org.freedesktop.portal.Desktop=org.freedesktop.portal.Settings.SettingChanged@/org/freedesktop/portal/desktop',
    ])

    # Add all the portals and busses the program needs.
    command.extend([f'--talk=org.freedesktop.portal.{portal}' for portal in portals])
    command.extend([f'--see={portal}' for portal in args["see"]])
    command.extend([f'--talk={portal}' for portal in args["talk"]])
    command.extend([f'--own={portal}' for portal in args["own"]])

    if args["xdg_open"]:
        command.extend(["--talk=org.freedesktop.portal.OpenURI"])
    if args["verbose"]:
        command.extend(["--log"])
    log(" ".join(command))
    Popen(command)


# Run the application.
def run_application(application, application_path, application_folder, info_name, temp):
    command = ["bwrap", "--new-session", "--die-with-parent"]
    local_dir = Path(data, "sb", application)
    if not local_dir.is_dir():
        local_dir.mkdir(parents=True)

    # Add the flatpak-info.
    if application_folder:
        command.extend(["--ro-bind-try", info_name, f"/run/{real}/flatpak-info"])
        command.extend(["--ro-bind-try", info_name, "/.flatpak-info"])

    # If we have a home directory, add it.
    if args["home"]:
        home_dir = Path(local_dir, "home")
        home_dir.mkdir(parents=True, exist_ok=True)

        # If it's cached, make a copy on a TMPFS.
        if args["cached_home"]:
            command.extend([
                "--overlay-src", str(home_dir),
                "--tmp-overlay", "/home",
            ])
        else:
            command.extend(["--bind", str(home_dir), "/home"])

    if args["share_cache"]:
        share(command, [f"{home}/.cache"], "bind")
    else:
        command.extend(["--bind", temp, "/home/sb/.cache"])

    # Add the tmpfs.
    command.extend(["--tmpfs", temp])

    # Environment variables should not be cached, since they can change at any time.
    # Therefore, we generate the environment variables for each launch.
    command.append("--clearenv")
    command.extend(env("XDG_RUNTIME_DIR") + env("XDG_CURRENT_DESKTOP") + env("DESKTOP_SESSION"))
    if application_folder:
        command.extend(["--setenv", "DBUS_SESSION_BUS_ADDRESS", session])
    if args["zsh"]:
        command.extend(env("SHELL"))
    if args["kde"]:
        command.extend(env("KDE_FULL_SESSION") + env("KDE_SESSION_VERSION"))
    if args["dri"]:
        command.extend(env("XDG_SESSION_DESKTOP"))
    if "wayland" in args["sockets"]:
        command.extend(env("WAYLAND_DISPLAY"))
    if "xorg" in args["sockets"]:
        command.extend(env("DISPLAY") + env("XAUTHORITY") + env("ICEAUTHORITY"))
    if args["locale"]:
        command.extend(env("LANG") + env("LANGUAGE"))

    # Get the cached portion of the command.
    command.extend(gen_command(application, application_path, application_folder))

    if args["hardened_malloc"]:
        # Typically, hardened malloc works by simply passing LD_PRELOAD
        # along --setenv. Even when running under zypak, the ZYPAK_LD_PRELOAD
        # does the job in Electron. However, and for a reason I'm not entirely sure,
        # Chromium will not use hardened malloc underneath zypak; being the most
        # important application sandbox, we've created a temporary ld.so.preload
        # for the sandbox. This "privileged preload" does not have the same
        # fragility of LD_PRELOAD (ID matching, being able to just unset it),
        # so it also improves the sandbox.
        preload = Path(data if args["sof"] == "data" else "/run" if args["sof"] == "zram" else "/tmp", "sb", application)
        preload.mkdir(exist_ok=True, parents=True)

        preload /= "ld.so.preload"
        preload.open("w").write("/usr/lib/libhardened_malloc.so\n")
        command.extend(["--ro-bind", str(preload.resolve()), "/etc/ld.so.preload"])

    post = []
    writeback = {}
    if args["file_passthrough"] != "off":
        enclave = TemporaryDirectory(prefix=application + "-", suffix="-enclave", dir=temp_dir)
        command.extend(["--bind", enclave.name, "/enclave"])

        mode = "--ro-bind-try" if args["file_passthrough"] == "ro" else "--bind-try"
        for source, write in [(args["unknown"], True), (args["files"], False)]:
            for argument in source:
                path = Path(argument)

                if path.is_dir() or path.is_file():
                    dest = "/enclave" + argument
                    if path.is_dir():
                        command.extend([mode, str(path), dest])

                    # If a file, we may do an enclave depending on the user.
                    elif path.is_file():
                        if args["file_passthrough"] == "writeback":
                            enclave_file = Path(str(enclave.name) + argument)
                            enclave_file.parent.mkdir(parents=True, exist_ok=True)
                            enclave_file.open("wb").write(path.open("rb").read())
                            run(["chmod", "666", enclave_file.resolve()])
                            writeback[enclave_file] = path
                        else:
                            command.extend([mode, str(path), dest])
                    if write:
                        post.append(dest)
                elif write:
                    post.append(argument)

    # Either launch the debug shell, run under strace, or run the command accordingly.
    if args["strace"]:
        command.extend(["strace", "-ff", "-v", "-s", "100"])

    if args["debug_shell"]:
        command.append("sh")
    else:
        command.append(application_path)
        command.extend(post)

    # So long as we aren't dry-running, run the program.
    if not args["dry"]:
        log("Command:", " ".join(command))
        run(command)

    # If we have RW access, and there's things in the enclave, update the source.
    for enclave_file, real_file in writeback.items():
        real_file.open("wb").write(enclave_file.open("rb").read())

    # Cleanup residual files from bind mounting
    run(["find", str(local_dir), "-size", "0", "-delete"])
    run(["find", str(local_dir), "-empty", "-delete"])

# Generate the cacheable part of the command.
def gen_command(application, application_path, application_folder):

    # Get all our locations.
    if args["sof"] == "data":
        sof_dir = Path(data, "sb", application, "sof")
    elif args["sof"] == "zram" and Path("/run", "sb").is_dir():
        sof_dir = Path("/run", "sb", application)
    else:
        sof_dir = Path("/tmp", "sb", application)

    log("SOF:", str(sof_dir))

    local_dir = Path(data, "sb", application)
    lib_cache = Path(local_dir, "lib.cache")
    cmd_cache = Path(local_dir, "cmd.cache")

    # Generate a hash of our command. We ignore switches
    # that do not change the output
    raw = args.copy()
    for arg in ["--verbose", "--startup", "--dry", "--update-libraries"]:
     if arg in raw:
        raw.remove(arg)
    h = new("SHA256")
    h.update("".join(raw).encode())
    hash = h.hexdigest()

    # If we don't explicitly want to update the libraries, the SOF exists, there is a library cache,
    # and the hash matches the command hash, just return the cached copy of both.
    update_sof = args["update_libraries"]
    if update_sof is False:
        if not sof_dir.is_dir():
            if lib_cache.is_file():
                log("Using cached library definitions")
                libraries.current = set([library for library in lib_cache.open("r").read().split(" ")])
            else:
                log("Creating library cache")
                update_sof = True
        else:
            log("Using pre-existing SOF")
    else:
        log("Regenerating library cache")


    if cmd_cache.is_file():
        with cmd_cache.open("r") as file:
            info = file.read().split("\n")
            cached = info[0]
            if hash == cached and sof_dir.is_dir() and not args["update_libraries"]:
                log("Using cached command")
                return info[1].split(" ")
            else:
                update_sof = True
                if hash != cached:
                    log("Outdated command cache. Updating...")
                elif not sof_dir.is_dir():
                    log("Updating command cache with library cache...")
                else:
                    log("Regenerating command cache")


    # Get the basic stuff.
    log("Setting up...")
    command = []

    local_runtime = runtime
    command.extend([
        "--setenv", "HOME", "/home/sb",
        "--setenv", "PATH", "/usr/bin",
    ])

    # Only necessary if we need portals
    if application_folder:
        command.extend([
            "--dir", local_runtime,
            "--chmod", "0700", local_runtime,
            "--ro-bind-try", f"{application_folder}/bus", f"{local_runtime}/bus",
            "--bind-try", f"{runtime}/doc", f"{local_runtime}/doc",
        ])
        share(command, ["/run/dbus"])
    else:
        command.extend(["--uid", nobody, "--gid", nobody,])


    if args["lib"]:
        share(command, ["/usr/lib"])
    else:
        command.extend(["--dir", "/usr/lib"])

    # Symlink lib (In the sandbox, not to the host)
    share(command, ["/etc/ld.so.preload", "/etc/ld.so.cache"])
    command.extend([
        "--symlink", "/usr/lib", "/lib",
        "--symlink", "/usr/lib", "/lib64",
        "--symlink", "/usr/lib", "/usr/lib64",
    ])

    if args["xdg_open"]:
        args["binaries"].extend(["xdg-open"])

    # Add python, if needed.
    if args["python"]:
        version = f"python{args["python"]}"
        args["binaries"].extend([version, "python"])
        if update_sof:
            libraries.current |= {f"lib{version}.so"}
            libraries.directories |= {f"/usr/lib/{version}/"}

    # Git
    if args["git"]:
        share(command, ["/dev/null"], "dev-bind")
        args["binaries"].extend([
            "git", "git-cvsserver", "git-receive-pack", "git-shell",
            "git-upload-archive", "git-upload-pack", "gitk", "scalar"
        ])
        libraries.directories |= {"/usr/lib/git-core/"}
        share(command, ["/usr/share/git/"])

    if args["hardened_malloc"]:
        command.extend(["--setenv", "LD_PRELOAD", "/usr/lib/libhardened_malloc.so"])
        libraries.wildcards.add("libhardened_malloc*")

    if args["zsh"]:
        share(command, [
            "/etc/shells", "/usr/share/zsh/", "/etc/zsh/",
            "/etc/profile", "/usr/share/terminfo", "/var/run/utmp",
            "/etc/group", f"{home}/.zshrc", f"{config}/environment.d"],
            "ro-bind-try"
        )

        args["devices"].extend(["/dev/pts/"])
        args["proc"] = True

        # For shell and coredump.
        args["binaries"].extend(["zsh", "mv"])
        libraries.directories |= {"/usr/lib/zsh/"}

    if args["include"]:
        args["ro"].extend([
            "/usr/include",
            "/usr/local/include"
        ])
        libraries.directories |= {"/usr/lib/clang", "/usr/lib/gcc"}

    # Add electron
    if args["electron"]:
        log("Adding electron...")

        # NSS junk
        if update_sof:
            libraries.wildcards.add("libsoftokn3*")
            libraries.wildcards.add("libfreeblpriv3*")
            libraries.wildcards.add("libsensors*")
            libraries.wildcards.add("libnssckbi*")

        share(command, ["/sys/block", "/sys/dev"])
        share(command, ["/dev/null", "/dev/urandom", "/dev/shm"], "dev-bind")

        if args["electron_version"]:
            # Extend existing args so they can be handled.
            if args["electron_version"] == "stable":
                electron_string = f"electron{environ["ELECTRON_VERSION"]}"
            else:
                electron_string = f"electron{args["electron_version"]}"

            args["binaries"].extend([
                f"/usr/lib/{electron_string}/electron",
                f"/usr/bin/{electron_string}",

            ])
            args["ro"].append(f"/usr/lib/{electron_string}")

            if update_sof:
                libraries.directories |= {f"/usr/lib/{electron_string}"}

        # Enable needed features.
        args["dri"] = True
        args["gtk"] = True
        if not args["proc"]:
            log("/proc is needed for electron applications. Enabling...")
            args["proc"] = True
        if "user" not in args["share"]:
            args["share"].append("user")

    # Add KDE
    if args["kde"]:
        log("Adding KDE...")
        share(command, [
            f"{config}/kdedefaults",
            f"{config}/breezerc",
            f"{config}/kcminputrc",
            f"{config}/kdeglobals",
            f"{config}/kwinrc",
            f"{config}/kxmlgui5",
            f"{config}/Trolltech.conf",
            f"/etc/xdg/{application}rc",
            f"{config}/kde.org",
        ])

        if update_sof:
            libraries.wildcards.add("libKF*")
            libraries.wildcards.add("lib*Kirigami*")
            libraries.directories |= {"/usr/lib/kf6/"}

        args["dri"] = True
        args["qt"] = True


    # Add QT
    if args["qt"] or args["qt5"]:
        log("Adding QT...")
        share(command, ["/usr/share/qt6"])
        if args["qt5"]:
            share(command, ["/usr/share/qt5"])
        if update_sof:
            libraries.directories |= {"/usr/lib/qt6/"}
            libraries.wildcards.add("libQt*")
            if args["qt5"]:
                libraries.directories |= {"/usr/lib/qt5/", "/usr/lib/qt/"}
        args["dri"] = True

    # Add GTK
    if args["gtk"]:
        log("Adding GTK...")
        share(command, [
            f"{home}/.gtkrc-2.0", f"{config}/gtkrc", f"{config}/gtkrc-2.0",
            f"{config}/gtk-2.0", f"{config}/gtk-3.0", f"{config}/gtk-4.0",
            "/usr/share/gtk-2.0",
            "/usr/share/gtk-3.0",
            "/usr/share/gtk-4.0",
            "/usr/share/gtk",
            "/usr/share/glib-2.0",
            "/etc/xdg/gtk-3.0/",
        ])
        command.extend(["--setenv", "GTK_USE_PORTAL", "1"])
        if update_sof:
            libraries.wildcards |= {
                "libgvfs*",
                "librsvg*",
                "libgio*",
                "libgdk*",
                "libgtk*"
            }
            libraries.directories |= {
                "/usr/lib/gdk-pixbuf-2.0/",
                "/usr/lib/gtk-3.0",
                "/usr/lib/tinysparql-3.0/",
                "/usr/lib/gio",
                "/usr/lib/gvfs"
            }
        args["dri"] = True

    if args["gst"]:
        args["binaries"].extend([
            "gst-inspect-1.0"
            "gst-launch-1.0"
            "gst-stats-1.0"
            "gst-tester-1.0"
            "gst-typefind-1.0"
        ])
        libraries.directories |= {"/usr/lib/girepository-1.0/", "/usr/lib/gstreamer-1.0/"}
        libraries.wildcards.add("libgst*")

        share(command, ["/usr/share/gir-1.0/", "/usr/share/gstreamer-1.0/"])


    # Mount devices, or the dev
    if args["dev"]:
        command.extend(["--dev", "/dev"])
    else:
        for device in args["devices"]:
            share(command, [device], "dev-bind-try")

    # Mount system directories.
    if args["proc"]:
        command.extend(["--proc", "/proc"])
    if args["etc"]:
        share(command, ["/etc"])
    if args["usr_share"]:
        share(command, ["/usr/share"])
    if args["sys"]:
        share(command, ["/sys"], "dev-bind-try")

    # Add DRI stuff.
    if args["dri"]:
        log("Adding DRI...")
        share(command, ["/dev/dri", "/dev/dri", "/dev/udmabuf"], "dev-bind-try")
        share(command, [
            "/sys/devices",
            "/etc/vulkan",
            "/usr/share/glvnd",
            "/usr/share/vulkan",
            f"{data}/vulkan",
            "/usr/share/libdrm",
            "/usr/share/libdrm/amdgpu.ids",
            "/sys/dev",
        ])
        share(command, [
                "/usr/share/fontconfig", "/usr/share/fonts", "/etc/fonts",
                f"{home}/.fonts",
                f"{config}/fontconfig", f"{data}/fontconfig", f"{cache}/fontconfig",
                "/usr/share/themes", "/usr/share/color-schemes", "/usr/share/icons", "/usr/share/cursors", "/usr/share/pixmaps",
                "/usr/share/mime",
                f"{data}/mime",
                f"{data}/pixmaps",
            ])
        if update_sof:
            for lib in [
                "libvulkan*", "libglapi*", "*mesa*", "*Mesa*", "libdrm", "libGLX*", "libEGL*",
                "libVkLayer*", "libgbm*", "libva*", "*egl*", "*EGL*"
                ]:
                    libraries.wildcards.add(lib)
            libraries.directories |= {"/usr/lib/dri", "/usr/lib/gbm"}

    # Add the wayland socket and XKB
    if "wayland" in args["sockets"]:
        command.extend(["--ro-bind-try", f"{runtime}/wayland-0", f"{local_runtime}/wayland-0"])
        share(command, [
            "/usr/share/X11/xkb",
            "/etc/xkb"
        ])
        command.extend(["--setenv", "XDG_SESSION_TYPE", "wayland"])


    # Add the pipewire socket, and its libraries.
    if "pipewire" in args["sockets"]:
        command.extend(["--ro-bind-try", f"{runtime}/pipewire-0", f"{local_runtime}/pipewire-0"])
        command.extend(["--ro-bind-try", f"{runtime}/pulse", f"{local_runtime}/pulse"])
        share(command, [
            f"{config}/pulse"
            "/etc/pipewire",
            "/usr/share/pipewire",
        ])
        if update_sof:
            libraries.wildcards.add("libpipewire*")
            libraries.directories |= {"/usr/lib/pipewire-0.3/", "/usr/lib/spa-0.2/", "/usr/lib/pulseaudio/"}

    # add Xorg. This is a vulnerability.
    if "xorg" in args["sockets"]:
        share(command, ["/tmp/.X11-unix/X0"])
        for xauth in output(["find", runtime, "-maxdepth", "1", "-name", "xauth_*"]):
            command.extend(["--ro-bind-try", xauth, f"{local_runtime}/{xauth.split("/")[-1]}"])

    # Extend the hostname if needed, otherwise use a fake one.
    if args["real_hostname"]:
        share(command, ["/etc/hostname"])
    else:
        command.extend(["--hostname", "sandbox"])

    # Add locale information
    if args["locale"]:
        share(command, [
            "/etc/locale.conf",
            "/etc/localtime",
            "/usr/share/zoneinfo",
            "/usr/share/X11/locale",
            "/usr/share/locale",
            f"{config}/plasma-localerc"
        ])
        if update_sof:
            libraries.directories |= {"/usr/lib/locale/"}
        args["binaries"].append("locale")

    # Hunspell
    if args["hunspell"]:
        share(command, ["/usr/share/hunspell", "/usr/share/myspell"])

    # Handle sharing namespaces.
    if args["share"] == "none":
        command.append("--unshare-all")
    elif args["share"] != "all":
        for shared in ["user", "ipc", "pid", "net", "uts", "cgroup"]:
            if shared not in args["share"]:
                command.append(f"--unshare-{shared}")

    # Networking adds the networking stack so it works properly.
    if "net" in args["share"]:
        if "--unshare-all" in command:
            command.append("--share-net")
        share(command, [
            "/etc/gai.conf", "/etc/hosts.conf", "/etc/hosts", "/etc/host.conf", "/etc/nsswitch.conf", "/etc/resolv.conf", "/etc/gnutls/config",
            "/etc/ca-certificates", "/usr/share/ca-certificates/",
            "/etc/pki", "/usr/share/pki",
            "/etc/ssl", "/usr/share/ssl",
        ])
        if update_sof:
            libraries.wildcards.add("libnss*")
    if "user" not in args["share"]:
        command.extend(["--disable-userns", "--assert-userns-disabled"])

    # Add variables
    for variable in args["env"]:
        if "=" in variable:
            split = variable.split("=")
            command.extend(["--setenv", split[0], "=".join(split[1:])])
        elif variable in environ:
            command.extend(["--setenv", variable, environ[variable]])
        else:
            log("Unrecognize environment variable:", variable)


    # Add binaries.
    if args["bin"]:
        log("Adding binaries...")
        share(command, ["/usr/bin"], "ro-bind")
    else:
        log("Generating binaries...")
        bins = set(args["binaries"])
        bins.add(application_path)

        # Add the debug shell
        if args["debug_shell"]:
            bins.add("sh")

        # Add strace
        if args["strace"]:
            bins.add("strace")

        # Add all the binaries
        for binary in bins:

            # Get the binary, and its dependencies
            binaries.add(binary)
        share(command, binaries.current, "ro-bind-try")
        for bin in binaries.current:
            libraries.current |= libraries.get(bin)
    for binary in args["local"]:
        for executable in output(["find", str(local_dir) + binary, "-mindepth", "1", "-executable", "-type", "f"]):
            share(command, binaries.parse(executable))


    command.extend(["--symlink", "/usr/bin", "/bin"])
    command.extend(["--symlink", "/usr/bin", "/bin"])
    command.extend(["--symlink", "/usr/bin", "/sbin"])
    command.extend(["--symlink", "/usr/bin", "/usr/sbin"])


    # If we aren't just passing through /lib, setup our SOF.
    if update_sof:
        for library in args["libraries"]:
            libraries.current |= libraries.get(library)
        for path in args["local"]:
            depends = libraries.get(str(local_dir) + path, local=True)
            libraries.current |= depends

    # If we're updating the SOF, generate all the libraries needed based on those that were explicitly provided.
    if not args["lib"]:
        path = Path(f"{str(sof_dir)}/usr/lib/")
        path.mkdir(parents=True, exist_ok=True)

        # We technically need /usr/lib to be mutable to create directories, but
        # rather than letting the program actually write to libraries in the temp
        # folder, which taint other instances of the program, we just use a
        # temporary overlay that discards those changes.
        command.extend(["--overlay-src", str(path), "--tmp-overlay", "/usr/lib"])

        # Using OverlayFS, we can keep both the main /usr/lib "RO", and the
        # subsequent sub-folders. To do this, we need two sources for the --ro-overlay
        # The first is the actual folder in /usr/lib/, which avoids us having to copy
        # files, and the second is an empty path in the SOF directory.
        if update_sof:
            for dir in libraries.directories:
                if Path(dir).is_dir():
                    libraries.current |= libraries.get(dir)
            share(command, libraries.directories, "ro-bind")
            libraries.setup(sof_dir, lib_cache, update_sof)

    # Setup application directories.
    if "config" in args["app_dirs"]:
        share(command, [f"{config}/{application}"], "bind-try")
    if "cache" in args["app_dirs"]:
        share(command, [f"{cache}/{application}"], "bind-try")
    if "share" in args["app_dirs"]:
        share(command, [f"/usr/share/{application}"], "bind-try")
    if "data" in args["app_dirs"]:
        share(command, [f"{data}/{application}"], "bind-try")
    if "etc" in args["app_dirs"]:
        share(command, [f"/etc/{application}"], "bind-try")
    if "lib" in args["app_dirs"]:
        share(command, [f"/usr/lib/{application}"])

    # Setup any paths explicitly provided.
    # Files must be simply bound in, which leads to unfortunate residual files
    # in the static SB in $XDG_DATA_HOME
    # Folder, however, we can overlay, which makes things a lot better.
    for path in args["rw"]:
        p = Path(path)
        if p.is_file() or p.is_dir():
            share(command, [path], "bind-try")
        else:
            log("Warning: path:", path, "Does not exist!")
    for path in args["ro"]:
        p = Path(path)
        if p.is_file() or p.is_dir():
            share(command, [path], "ro-bind-try")
        else:
            log("Warning: path:", path, "Does not exist!")

    # Write the command cache.
    with cmd_cache.open("w") as file:
        log("Writing command cache")
        file.write(hash)
        file.write("\n")
        file.write(" ".join(command))
    return command


if __name__ == "__main__":
    main()
