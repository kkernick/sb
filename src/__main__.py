#!/bin/python

from os import environ
from subprocess import Popen, run
from pathlib import Path
from tempfile import TemporaryDirectory
from hashlib import new

from shared import args, log, share, cache, config, data, home, runtime, session, nobody, real, env, resolve, sof, profile, profile_start
from util import desktop_entry
from syscalls import syscall_groups

import binaries
import libraries

from inotify_simple import INotify, flags
inotify = INotify()


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

    app_sof = Path(str(sof), program)
    app_sof.mkdir(exist_ok=True)

    work_dir=TemporaryDirectory(prefix="", dir=str(app_sof))

    if not args["portals"] and not args["see"] and not args["talk"] and not args["own"]:
        log("No portals requiried.")
        portals = False
        proxy_wd = 0
    else:
        profile_start()
        log("Creating dbus proxy...")

        # Write the .flatpak-info file so the application thinks its running in flatpak and thus portals work.
        # https://docs.flatpak.org/en/latest/flatpak-command-reference.html#flatpak-metadata
        with open(work_dir.name + "/.flatpak-info", "w") as file:
            file.write("[Application]\n")
            file.write(f"name=app.appliction{program}\n")

            file.write("[Context]\n")
            file.write("sockets=session-bus;system-bus;")
            if "wayland" in args["sockets"]:
                file.write("wayland;")
            if "pipewire" in args["sockets"]:
                file.write("pulseaudio;")

            file.write("[Instance]\n")
            file.write(f"instance-id={program}\n")
            file.write("session-bus-proxy=True\n")
            file.write("system-bus-proxy=True\n")

            file.write("[Environment]\n")
            for env in args["env"]:
                file.write(f"{env}\n")

        # Setup the DBux Proxy. This is needed to mediate Dbus calls, and enable Portals.
        log("Launching D-Bus Proxy")
        proxy_wd = dbus_proxy(args["portals"], program, work_dir)
        profile("Launch D-Bus Proxy")
        portals = True

    # Then, run the program.
    log("Launching ", program, "at", path)
    run_application(program, path, work_dir, portals, proxy_wd)


# Run the DBUS Proxy.
def dbus_proxy(portals, program, work_dir):
    command = ["bwrap"]
    if args["hardened_malloc"]:
        command.extend(["--setenv", "LD_PRELOAD", "/usr/lib/libhardened_malloc.so"])

    proxy = Path(work_dir.name + "/proxy")
    proxy.mkdir()
    wd = inotify.add_watch(str(proxy), flags.CREATE)

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
        "--ro-bind", work_dir.name + "/.flatpak-info", "/.flatpak-info",
        "--symlink", "/.flatpak-info", f"/run/user/{real}/flatpak-info",
        "--bind", work_dir.name + "/proxy", f"{runtime}/app/app.application.{program}",
        "--die-with-parent",
        "--",
        "xdg-dbus-proxy",
        environ["DBUS_SESSION_BUS_ADDRESS"],
        f"{runtime}/app/app.application.{program}/bus",
        "--filter",
        '--call=org.freedesktop.portal.Desktop=org.freedesktop.portal.Settings.Read@/org/freedesktop/portal/desktop',
        '--broadcast=org.freedesktop.portal.Desktop=org.freedesktop.portal.Settings.SettingChanged@/org/freedesktop/portal/desktop',
    ])


    if args["xdg_open"]:
        portals.append("OpenURI")

    # Add all the portals and busses the program needs.
    command.extend([f'--talk=org.freedesktop.portal.{portal}' for portal in portals])
    command.extend([f'--see={portal}' for portal in args["see"]])
    command.extend([f'--talk={portal}' for portal in args["talk"]])
    command.extend([f'--own={portal}' for portal in args["own"]])

    if args["verbose"]:
        command.extend(["--log"])
    log(" ".join(command))
    Popen(command)
    return wd


# Run the application.
def run_application(application, application_path, work_dir, portals, proxy_wd):
    command = ["bwrap", "--new-session", "--die-with-parent"]
    local_dir = Path(data, "sb", application)
    if not local_dir.is_dir():
        local_dir.mkdir(parents=True)

    # If we have a home directory, add it.
    if args["fs"] != "none":
        fs = Path(local_dir, args["fs_location"])
        fs.mkdir(parents=True, exist_ok=True)

        home = fs / "home" / "sb"
        home.mkdir(parents=True, exist_ok=True)

        if args["fs"] == "cache":
            command.extend([
                "--overlay-src", str(fs),
                "--tmp-overlay", "/",
            ])
        else:
            command.extend(["--bind", str(fs), "/"])

    if portals and not args["no_flatpak"]:
        command.extend(["--ro-bind", work_dir.name + "/.flatpak-info", "/.flatpak-info"])
        command.extend(["--symlink", "/.flatpak-info", f"/run/user/{real}/flatpak-info"])
    else:
        log("Application not using portals")

    # Add the tmpfs.
    if not args["no_tmpfs"]:
        command.extend(["--tmpfs", "/home/sb/.cache"])
        command.extend(["--tmpfs", "/tmp"])

    # Environment variables should not be cached, since they can change at any time.
    # Therefore, we generate the environment variables for each launch.
    command.append("--clearenv")
    command.extend(env("XDG_RUNTIME_DIR") + env("XDG_CURRENT_DESKTOP") + env("DESKTOP_SESSION"))
    if portals:
        command.extend(["--setenv", "DBUS_SESSION_BUS_ADDRESS", session])

    if args["shell"]:

        # Trying to support every possible shell and all its configuration is outside the scope of SB
        # Shells can be manually added by the user.
        command.extend(["--setenv", "SHELL", "/usr/bin/sh"])

        # Some applications source /etc/passwd for shell information. Because we abstract the real
        # user to an SB with the same UID (Unless no Portals), we want to create a fake passwd
        # that includes the information it wants.
        with open(work_dir.name + "/passwd", "w") as file:
            file.write("[Application]\n")
            file.write(f"sb:x:{real}:{real}:SB:/home/sb:/usr/bin/sh\n")
        command.extend(["--ro-bind", work_dir.name + "/passwd", "/etc/passwd"])

    if args["kde"]:
        command.extend(env("KDE_FULL_SESSION") + env("KDE_SESSION_VERSION"))
    if args["dri"]:
        command.extend(env("XDG_SESSION_DESKTOP"))
    if "wayland" in args["sockets"]:
        command.extend(env("WAYLAND_DISPLAY"))
    if args["locale"]:
        command.extend(env("LANG") + env("LANGUAGE"))


    # Only necessary if we need portals
    if portals:
        command.extend([
            "--dir", runtime,
            "--chmod", "0700", runtime,
            "--ro-bind", f"{work_dir.name}/proxy/bus", f"{runtime}/bus",
            "--bind", f"{runtime}/doc", f"{runtime}/doc",
        ])
        share(command, ["/run/dbus"])
    else:
        command.extend(["--uid", nobody, "--gid", nobody,])

    # Get the cached portion of the command.
    lock_file = Path(str(sof), application, "sb.lock")

    # Wait for the lock to be released.
    if lock_file.is_file():
        wd = inotify.add_watch(str(sof) + "/" + application, flags.DELETE)
        while lock_file.is_file():
            for src, _, _, event in inotify.read():
                if src == wd and event == "sb.lock":
                    inotify.rm_watch(wd)
                    break
    lock = lock_file.open("x")

    try:
        profile_start()
        command.extend(gen_command(application, application_path))
        profile("Command Generation")
    except Exception as e:
        print("Failed to generate bubblewrap command:", e)
        lock.close()
        lock_file.unlink()
        exit(1)

    lock.close()
    lock_file.unlink()
    if args["hardened_malloc"]:
        with open(work_dir.name + "/ld.so.preload", "w") as file:
            file.write("/usr/lib/libhardened_malloc.so\n")
        command.extend(["--ro-bind", work_dir.name + "/ld.so.preload", "/etc/ld.so.preload"])

    # If the user is either logging, has syscalls defined, or has a file, create a SECCOMP Filter.
    syscall_file = Path(data, "sb", application, "syscalls.txt")
    if not args["debug_shell"] and (args["syscalls"] or syscall_file.is_file() or args["seccomp_log"]):
        profile_start()

        if not syscall_file.is_file():
            syscall_file.open("x")
        lines = syscall_file.open("r").readlines()
        total = 0
        h = new("SHA256")
        h.update(" ".join(lines).encode())
        hash = h.hexdigest()

        cached = Path(local_dir, "seccomp.cache")
        filter = Path(local_dir, "filter.bpf")

        if not cached.is_file() or not filter.is_file() or cached.open().read() != hash or args["update_cache"] or args["seccomp_group"]:
            log("Generating new BPF Filter")

            # Combine our sources into a single list.
            syscalls = set(args["syscalls"])
            if syscall_file.is_file():
                for line in lines:
                    for split in line.split(" "):
                        stripped = split.strip("\n \t")
                        if stripped:
                            syscalls.add(stripped)
                            total += len(stripped)
            else:
                total += 1

            log("Permitted Syscalls:", total, f"({"LOG" if args["seccomp_log"] else "ENFORCED"})")

            # Create our output and filter.
            from seccomp import SyscallFilter, Attr, ALLOW, LOG, ERRNO
            from errno import EPERM

            filter_bpf = filter.open("w")
            new_filter = SyscallFilter(defaction=LOG if args["seccomp_log"] else ERRNO(EPERM))

            # Enforce that children have a subset of parent permissions.
            new_filter.set_attr(Attr.CTL_NNP, True)

            # Synchronize threads to the seccomp-filter.
            new_filter.set_attr(Attr.CTL_TSYNC, True)

            # Our filters aren't particularly complicated, so using priority and complexity isn't
            # the best method for compiling the filter. Instead, a binary tree will do much better
            new_filter.set_attr(Attr.CTL_OPTIMIZE, 2)

            if args["verbose"]:
                new_filter.set_attr(Attr.CTL_LOG, True)

            # Add syscalls
            included = set()
            def add_rule(syscall):
                if syscall in included:
                    return
                included.add(syscall)
                try:
                    try:
                        new_filter.add_rule(ALLOW, int(syscall))
                    except Exception:
                        new_filter.add_rule(ALLOW, syscall)
                except Exception as e:
                    print("INVALID syscall:", syscall, ":", e)
                    exit(1)

            # Add each syscall, attempting to convert them into integers as both formats are supported.
            for syscall in syscalls:
                if syscall in syscall_groups:
                    for s in syscall_groups[syscall]:
                        add_rule(s)
                else:
                    add_rule(syscall)

            if args["seccomp_group"]:
                groups = set()
                for syscall in included:
                    added = False
                    for key, value in syscall_groups.items():
                        if syscall in value:
                            groups.add(key)
                            added = True
                            break
                    if not added:
                        groups.add(syscall)
                with syscall_file.open("w") as file:
                    for group in groups:
                        file.write(group + " ")
                    file.close()

            # Export, add it as stdin.
            new_filter.export_bpf(filter_bpf)
            filter_bpf.flush()
            filter_bpf.close()
            cached.open("w").write(hash)

        filter_bpf = filter.open("rb")
        command.extend(["--seccomp", str(filter_bpf.fileno())])
        profile("SECCOMP Filter Generation")
    else:
        filter_bpf = None

    # Now we iterate through unrecognized files and explicit files, adding them to the post command (After
    # the application such they are used as arguments). If we're using writeback, we keep a list of files
    # that need to be updated after the program closes.
    post = []
    if args["file_passthrough"] != "off":
        mode = "--ro-bind" if args["file_passthrough"] == "ro" else "--bind"

        for source, write in [(args["unknown"], True), (args["files"], False)]:
            for argument in source:
                path = Path(argument)

                if path.is_dir() or path.is_file():
                    dest = "/enclave/" + argument
                    if path.is_dir():
                        command.extend([mode, str(path), dest])

                    # If a file, we may do an enclave depending on the user.
                    elif path.is_file():
                        command.extend([mode, str(path), dest])
                    if write:
                        post.append(dest)
                elif write:
                    post.append(argument)

    if args["debug_shell"]:
        command.append("sh")
    else:
        if args["strace"]:
            command.extend(["strace", "-ff", "-v", "-s", "100"])

        if args["command"]:
            command.append(args["command"])
        else:
            command.append(application_path)

        command.extend(args["args"])
        command.extend(post)

    # So long as we aren't dry-running, run the program.
    if not args["dry"]:
        log("Command:", " ".join(command))

        if portals:
            profile_start()
            ready = False
            while not ready:
                for src, _, _, event in inotify.read():
                    if src == proxy_wd and event == "bus":
                        inotify.rm_watch(proxy_wd)
                        ready = True
                        break
            profile("Waiting for D-Bus Proxy")

        if filter_bpf:
            sandbox = Popen(command, pass_fds=[filter_bpf.fileno()])
        else:
            sandbox = Popen(command)

        if args["post_command"]:
            post = [args["post_command"]] + args["post_args"]
            log("Post:", " ".join(post))
            run(post)
            sandbox.terminate()
        else:
            sandbox.wait()

    # Cleanup residual files from bind mounting
    run(["find", str(local_dir), "-size", "0", "-delete"])
    run(["find", str(local_dir), "-empty", "-delete"])


# Generate the cacheable part of the command.
def gen_command(application, application_path):

    # Get all our locations.
    sof_dir = sof / application

    if args["sof"] == "data":
        sof_dir /= "sof"

    log("SOF:", str(sof_dir))

    local_dir = Path(data, "sb", application)
    lib_dir = sof_dir / "usr" / "lib"
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
        if not lib_dir.is_dir():
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
            if hash == cached and lib_dir.is_dir() and not args["update_libraries"]:
                log("Using cached command")
                return info[1].split(" ")
            else:
                update_sof = True
                if hash != cached:
                    log("Outdated command cache. Updating...")
                elif not lib_dir.is_dir():
                    log("Updating command cache with library cache...")
                else:
                    log("Regenerating command cache")



    # Get the basic stuff.
    log("Setting up...")
    command = []

    command.extend([
        "--setenv", "HOME", "/home/sb",
        "--setenv", "PATH", "/usr/bin",
    ])

    if args["lib"]:
        share(command, ["/usr/lib"])
    else:
        command.extend(["--dir", "/usr/lib"])

    command.extend([
        "--symlink", "/usr/lib", "/lib",
        "--symlink", "/usr/lib", "/lib64",
        "--symlink", "/usr/lib", "/usr/lib64",
    ])


    if args["xdg_open"]:
        args["binaries"].append("sb-open")
        command.extend(["--symlink", "/usr/bin/sb-open", "/usr/bin/xdg-open"])


    # Add python, if needed.
    if args["python"]:
        version = f"python{args["python"]}"
        args["binaries"].extend([version, "python"])
        if update_sof:
            libraries.current |= {f"lib{version}.so"}
            libraries.directories |= {f"/usr/lib/{version}/"}

    if args["hardened_malloc"]:
        command.extend(["--setenv", "LD_PRELOAD", "/usr/lib/libhardened_malloc.so"])
        libraries.wildcards.add("libhardened_malloc*")

    if args["shell"] or args["debug_shell"]:
        share(command, [
            "/etc/shells",
            "/etc/profile", "/usr/share/terminfo", "/var/run/utmp",
            "/etc/group", f"{config}/environment.d",
        ])
        libraries.directories |= {"/usr/lib/terminfo"}
        args["dev"] = True
        args["proc"] = True
        args["binaries"].extend(["sh"])
        if args["shell"]:
            log("WARNING: --shell requires global /dev and /proc access. This means full access to system devices!")

    if args["include"]:
        args["ro"].extend([
            "/usr/include",
            "/usr/local/include"
        ])
        libraries.directories |= {"/usr/lib/clang", "/usr/lib/gcc"}

    # Add electron
    if args["electron"]:
        log("Adding electron...")
        share(command, ["/sys/block", "/sys/dev"])
        share(command, ["/dev/null", "/dev/urandom", "/dev/shm"], "dev-bind")
        # NSS junk
        if update_sof:
            libraries.wildcards.add("libsoftokn3*")
            libraries.wildcards.add("libfreeblpriv3*")
            libraries.wildcards.add("libsensors*")
            libraries.wildcards.add("libnssckbi*")

        if args["electron_version"]:
            electron_string = f"electron{args["electron_version"]}"

            args["binaries"].extend([
                f"/usr/lib/{electron_string}/electron",
                f"/usr/bin/{electron_string}",

            ])
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
            libraries.directories |= {"/usr/lib/kf6/"}
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
            f"{config}/dconf",
            "/usr/share/gtk-2.0",
            "/usr/share/gtk-3.0",
            "/usr/share/gtk-4.0",
            "/usr/share/gtk",
            "/usr/share/glib-2.0",
            "/etc/xdg/gtk-3.0/",
            "/usr/share/gir-1.0/",
        ])
        command.extend(["--setenv", "GTK_USE_PORTAL", "1", "--setenv", "GTK_A11Y", "none"])
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
                "/usr/lib/gtk-4.0",
                "/usr/lib/gio",
                "/usr/lib/gvfs",
                "/usr/lib/gconv",
                "/usr/lib/girepository-1.0",
            }
        args["dri"] = True

    if args["gst"]:
        libraries.wildcards |= {"libgst*"}
        libraries.directories |= {"/usr/lib/gstreamer-1.0/"}
        share(command, ["/usr/share/gstreamer-1.0/"])

    if args["webkit"]:
        libraries.directories |= {"/usr/lib/webkitgtk-6.0"}
        libraries.wildcards |= {
            "libjavascriptcore*",
            "libwebkit*",
        }
        share(command, ["/sys/block", "/sys/dev"])
        share(command, ["/dev/null", "/dev/urandom", "/dev/shm"], "dev-bind")
        if not args["proc"]:
            log("/proc is needed for Webkit applications. Enabling...")
            args["proc"] = True
        if not args["bwrap"]:
            log("bwrap is needed for Webkit")
            args["bwrap"] = True
        if not args["no_flatpak"]:
            print("ERROR: WebKit requires --no-flatpak. This cannot be applied retroactively")
            exit(1)

    if args["bwrap"]:
        args["binaries"] += ["bwrap", "xdg-dbus-proxy"]
        args["devices"] += ["/dev/snd", "/dev/zero", "/dev/full", "/dev/random", "/dev/tty"]
        share(command, ["/sys/bus", "/sys/class"])
        if "user" not in args["share"]:
            args["share"].append("user")


    # Mount devices, or the dev
    if args["dev"]:
        command.extend(["--dev", "/dev"])
    else:
        for device in args["devices"]:
            share(command, [device], "dev-bind")

    # Mount system directories.
    if args["proc"]:
        command.extend(["--proc", "/proc"])

    # Add DRI stuff.
    if args["dri"]:
        log("Adding DRI...")
        share(command, ["/dev/dri", "/dev/dri", "/dev/udmabuf"], "dev-bind")
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
                "libvulkan*", "libglapi*", "*mesa*", "*Mesa*", "libdrm", "libGL*", "libEGL*",
                "libVkLayer*", "libgbm*", "libva*", "*egl*", "*EGL*",
                ]:
                    libraries.wildcards.add(lib)
            libraries.directories |= {"/usr/lib/dri", "/usr/lib/gbm"}

    # Add the wayland socket and XKB
    if "wayland" in args["sockets"]:
        command.extend(["--ro-bind", f"{runtime}/wayland-0", f"{runtime}/wayland-0"])
        share(command, ["/usr/share/X11/xkb", "/etc/xkb"])
        command.extend(["--setenv", "XDG_SESSION_TYPE", "wayland"])


    # Add the pipewire socket, and its libraries.
    if "pipewire" in args["sockets"]:
        command.extend(["--ro-bind", f"{runtime}/pipewire-0", f"{runtime}/pipewire-0"])
        command.extend(["--ro-bind", f"{runtime}/pulse", f"{runtime}/pulse"])
        share(command, [f"{config}/pulse", "/etc/pipewire", "/usr/share/pipewire"])
        if update_sof:
            libraries.wildcards.add("libpipewire*")
            libraries.directories |= {"/usr/lib/pipewire-0.3/", "/usr/lib/spa-0.2/", "/usr/lib/pulseaudio/"}

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

    # Hunspell/Enchant
    if args["spelling"]:
        share(command, ["/usr/share/hunspell", "/usr/share/myspell", "/usr/share/enchant-2"])
        libraries.directories |= {"/usr/lib/enchant-2"}
        libraries.wildcards |= {"libhunspell*", "libenchant*"}

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
            "/etc/gai.conf", "/etc/hosts.conf", "/etc/hosts", "/etc/host.conf", "/etc/nsswitch.conf", "/etc/resolv.conf",
            "/etc/gnutls",
            "/etc/ca-certificates", "/usr/share/ca-certificates/",
            "/etc/pki", "/usr/share/pki",
            "/etc/ssl", "/usr/share/ssl",
            "/usr/share/p11-kit",
        ])
        if update_sof:
            libraries.wildcards |= {"libnss*", "libgnutls*"}
            libraries.directories |= {"/usr/lib/pkcs11"}
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
        share(command, ["/usr/bin"])
    else:
        log("Generating binaries...")
        bins = set(args["binaries"])
        bins.add(application_path)

        # Add strace
        if args["strace"]:
            bins.add("strace")

        # Add all the binaries
        for binary in bins:

            # Get the binary, and its dependencies
            binaries.add(binary)
        share(command, binaries.current)
        if update_sof:
            for bin in binaries.current:
                libraries.current |= libraries.get(bin)

    command.extend(["--symlink", "/usr/bin", "/bin"])
    command.extend(["--symlink", "/usr/bin", "/bin"])
    command.extend(["--symlink", "/usr/bin", "/sbin"])
    command.extend(["--symlink", "/usr/bin", "/usr/sbin"])


    # If we aren't just passing through /lib, setup our SOF.
    if update_sof:
        for library in args["libraries"]:
            libraries.current |= libraries.get(library)

    # If we're updating the SOF, generate all the libraries needed based on those that were explicitly provided.
    if not args["lib"]:
        # We technically need /usr/lib to be mutable to create directories, but
        # rather than letting the program actually write to libraries in the temp
        # folder, which taint other instances of the program, we just use a
        # temporary overlay that discards those changes.
        command.extend(["--overlay-src", str(lib_dir), "--tmp-overlay", "/usr/lib"])

        # Using OverlayFS, we can keep both the main /usr/lib "RO", and the
        # subsequent sub-folders. To do this, we need two sources for the --ro-overlay
        # The first is the actual folder in /usr/lib/, which avoids us having to copy
        # files, and the second is an empty path in the SOF directory.
        valid = []
        for dir in libraries.directories:
            if Path(dir).is_dir():
                if update_sof:
                    libraries.current |= libraries.get(dir)
                valid.append(dir)
        share(command, valid)
        if update_sof:
            libraries.setup(sof_dir, lib_cache, update_sof)

    # Setup application directories.
    if "config" in args["app_dirs"]:
        share(command, [f"{config}/{application}"], "bind")
    if "cache" in args["app_dirs"]:
        share(command, [f"{cache}/{application}"], "bind")
    if "share" in args["app_dirs"]:
        share(command, [f"/usr/share/{application}"], "bind")
    if "data" in args["app_dirs"]:
        share(command, [f"{data}/{application}"], "bind")
    if "etc" in args["app_dirs"]:
        share(command, [f"/etc/{application}"], "bind")
    if "lib" in args["app_dirs"]:
        share(command, [f"/usr/lib/{application}"])

    # Setup any paths explicitly provided.
    # Files must be simply bound in, which leads to unfortunate residual files
    # in the static SB in $XDG_DATA_HOME
    # Folder, however, we can overlay, which makes things a lot better.
    for path in args["rw"]:
        src, dest = resolve(path)
        p = Path(src)
        if p.is_file() or p.is_dir():
            share(command, [path], "bind")
        else:
            log("Warning: path:", path, "Does not exist!")
    for path in args["ro"]:
        src, dest = resolve(path)
        p = Path(src)
        if p.is_file() or p.is_dir():
            share(command, [path])
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
