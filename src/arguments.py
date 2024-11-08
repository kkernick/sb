"""
@brief This file contains the ArgumentParser logic.
"""


from argparse import ArgumentParser
from pathlib import Path
from sys import argv
from os import environ


def parse():
    """
    @brief Parse the command line arguments.
    @returns The parsed arguments.
    """

    # Setup the parser.
    parser = ArgumentParser(prog="sb", description="Run applications in a Sandbox")

    # The program that we run in the sandbox.
    parser.add_argument("program", help="The program to run")

    """
    Portals are configured in the dbus proxy, which controls which interfaces the application
    can communicate over.
    """

    # Some standard Freedesktop Portals. Technically, you can add these explicitly in the --talk argument,
    # but they are presented for convenience.
    parser.add_argument(
        "--portals",
        action="extend",
        nargs="*",
        default=[],
        help="A list of XDG Portals to expose to the application",
    )

    # See, talk, or own busses
    parser.add_argument("--see", action="extend", nargs="*", default=[], help="A list of additional buses that the application should be able to see")
    parser.add_argument("--talk", action="extend", nargs="*", default=[], help="A list of additional buses that the application should be able to talk over")
    parser.add_argument("--own", action="extend", nargs="*", default=[], help="A list of additional buses that the application should be able to own")

    # Share namespaces. All and None are unique, defaulting to the latter. I've never needed to
    # enable namespaces for any application.
    parser.add_argument(
        "--share",
        action="extend",
        nargs="*",
        choices=["user", "ipc", "pid", "net", "uts", "cgroup", "all", "none"],
        default=["none"],
        help="A list of namespaces to share with the sandbox"
    )

    # "Sockets"
    # The Wayland and Pipewire sockets are supported.
    # Xorg is a vulnerability, but Chromium needs it (Even when on Wayland) for VA-API on AMD.
    parser.add_argument(
        "--sockets",
        action="extend",
        nargs="*",
        choices=["wayland", "pipewire", "xorg"],
        default=["wayland"],
        help="A list of sockets to give the application various functionality"
    )

    # Specify what binaries should be permitted.
    # --bin will merely mount /bin.
    # --binaries will use `which` to locate the binary, and will perform library dependency lookups.
    parser.add_argument("--bin", action="store_true", default=False, help="Mount /bin")
    parser.add_argument("--binaries", action="extend", nargs="*", default=[], help="A list of binaries to include in the sandbox. Does nothing if --bin")

    # Specify libraries.
    # By default, SB will dynamically determine libraries.
    # --lib will forgo this and simply mount /lib
    # --libraries allows you to explicitly specify needed libraries, often useful when using an interpreted program like Python.
    # You can either explicitly list the path (IE /usr/lib/mylib.so), just provide the library name (IE mylib.so), or use a wildcard
    # for matching multiply versions (IE mylib* -> mylib.so, mylib.so.0)
    # the folder rather than determining required libraries from within it. Unfortunately, some applications, such as QMMP, don't
    # appreciate this, because other libraries were missing. --recursive-libraries will perform the library lookup on the folder, and
    # any folder within /lib, but it will be slow to generate the command and library cache.
    parser.add_argument("--lib", action="store_true", default=False, help="Mount /lib and others")
    parser.add_argument("--libraries", action="extend", nargs="*", default=[], help="A list of libraries needed in the sandbox")
    parser.add_argument("--local", action="extend", nargs="*", default=[], help="A list of directories within the sandbox that the application creates, and needs library lookup.")

    parser.add_argument("--sof", action="store", default="tmpfs", choices=["tmpfs", "data", "zram"], help="Where the library SOF should be stored.")

    parser.add_argument("--ignore", action="extend", nargs="*", default=[], help="Ignore libraries/binaries")

    # Force the program to recalculate library dependencies, overwriting the library and command cache.
    parser.add_argument("--update-libraries", action="store_true", default=False, help="Update SOF libraries")
    parser.add_argument("--update-cache", action="store_true", default=False, help="Update SOF libraries")

    # Passthrough environment variables using ENV=VALUE.
    parser.add_argument("--env", action="extend", nargs="*", default=[], help="ENV=VALUE pairs to pass to the sandbox.")

    # Arbitrary paths that should be provided read-only or read-write. These are bind mounts.
    parser.add_argument("--rw", action="extend", nargs="*", default=[], help="Files/Directories the application can read and write.")
    parser.add_argument("--ro", action="extend", nargs="*", default=[], help="Files/Directories that the application can read.")

    # Passthrough various appdirs based on the command name.
    # config = ~/.config/application
    # cache = ~/.cache/application
    # etc = /etc/application
    # share = /usr/share/application
    # data = ~/.local/share/application
    # lib = /usr/lib/application
    parser.add_argument(
        "--app-dirs",
        action="extend",
        nargs="*",
        default=[],
        choices=["config", "cache", "etc", "share", "data", "lib"],
        help="Directories the application owns",
    )

    # Either passthrough the entire /dev, or individual devices.
    parser.add_argument("--dev", action="store_true", default=False, help="Mount the /dev filesystem")
    parser.add_argument("--devices", action="extend", nargs="*", default=[], help="A list of devices to include in the sandbox")


    # Root folders
    parser.add_argument("--proc", action="store_true", default=False, help="Mount the /proc filesystem")
    parser.add_argument("--etc", action="store_true", default=False, help="Mount the /etc folder")
    parser.add_argument("--sys", action="store_true", default=False, help="Mount the /sys folder")
    parser.add_argument("--usr-share", action="store_true", default=False, help="Give the application access to /usr/share")

    # Share files needed for GPU acceleration.
    parser.add_argument("--dri", action="store_true", default=False, help="Give the application access to DRI/GPU, Vulkan, Devices, and graphical things")

    # Share QT libraries and configurations.
    parser.add_argument("--qt", action="store_true", default=False, help="Give the application access to QT libraries (Implies --dri)")
    parser.add_argument("--qt5", action="store_true", default=False, help="Give legacy qt5 access")

    # Ensure Electron applications are supported. Will share proc, dri, and gtk.
    parser.add_argument("--electron", action="store_true", default=False, help="Give the application access to electron libraries (Implies --dri, --gtk)")

    # If you are actually using an electron app with the system electron, provide the version to make sure its available.
    parser.add_argument("--electron-version", default=None, help="Give the application access to a specific electron version. Only if --electron")

    # Bring in Python for a specific version.
    parser.add_argument("--python", default=None, help="Pass through the specified version of python, IE 3.12")

    # Bring in KDE/KF6 files and configurations.
    parser.add_argument("--kde", action="store_true", default=False, help="Give the application access to KDE/QT configuration (Implies --dri, --qt)")

    # Bring in GTK files and configuration. This isn't well tested besides in use of Electron.
    parser.add_argument("--gtk", action="store_true", default=False, help="Give the application access to GTK3/4 configuration (Implies --dri)")

    # ZSH
    parser.add_argument("--zsh", action="store_true", default=False, help="Give the application the ZSH shell.")

    # Include headers for C/C++
    parser.add_argument("--include", action="store_true", default=False, help="Give the application include headers, for clangd and other development tools")

    # Home related settings.
    # --home will create a home directory for the application. This lets configuration persist, but keeps it separate from your
    # actual home directory.
    # --cached-home will take this home directory and make a copy of it in RAM for each instance of the application. This is useful
    # if you have a good configuration, and don't want new changes to persist.
    parser.add_argument("--home", action="store_true", default=False, help="Create a permanent home directory for the application at $XDG_DATA_HOME/sb/application")
    parser.add_argument("--cached-home", action="store_true", default=False, help="Copy the SOF home into the application's temporary RAM folder. Modifications to the home will not persist, but will be isolated from instances and faster. Applications that use profile locks, like Chrome, can run multiple instances at once.")

    # Some applications, like Fooyin, store files in the cache, such as Thumbnails, to which the path
    # is not actually correct, since it's cache is not the actual home cache. This gives the application
    # access to the actual user ~/.cache folder, which while giving the application some access, is
    # usually quite safe.
    parser.add_argument("--share-cache", action="store_true", default=False, help="Give the application to the non-sandboxed .cache folder in the home directory.")

    # How the Sandbox should handle file arguments.
    # By default, any file argument passed to the command (IE application.sb ~/myfile) will be read-only bind mounted into the
    # sandbox, so that the application can read it.
    # --do-not-allow-file-passthrough will prohibit this functionality
    # --file-passthrough-rw will allow the application modification of this file.
    # --file-enclave is useful if permissions or other issues prevent the traditional passthrough from functioning (Noteably
    # applications that do not directly write to files, but instead create a temporary, and move that file to overwrite the
    # original. A file enclave will copy all passed files to a TMPFS, allow the program to modify it, and then update those files
    # once the program closes.
    parser.add_argument("--do-not-allow-file-passthrough", action="store_true", default=False, help="If the program is passed an argument to a file, do not expose this file to the sandbox.")
    parser.add_argument("--file-passthrough-rw", action="store_true", default=False, help="File passthrough is done with write privileges")
    parser.add_argument("--file-enclave", action="store_true", default=False, help="Pass through files in a mediated enclave. This ensures that programs will have sufficient permission to read/write, but the real file will not be updated until the program closes.")

    # Open a debug shell into the sandbox, rather than running the program. You may need to run --update-libraries to ensure
    # the shell program has needed libraries.
    parser.add_argument("--debug-shell", action="store_true", default=False, help="Launch a shell in the sandbox instead of the application")

    # Run the program in the sandbox under strace to debug missing files.
    parser.add_argument("--strace", action="store_true", default=False, help="Launch the application underneath strace")

    # Use zypak for chromium. This supercedes the deprecated --enable-namespaces flag, as it
    # does not give the application dangerous permissions in the sandbox.
    parser.add_argument("--zypak", action="store_true", default=False, help="Launch the application underneath zypak")

    # Give the application xdg-open to open files with the default application.
    parser.add_argument("--xdg-open", action="store_true", default=False, help="Give the application xdg-open to launch default applications")

    # Use the real hostname of the computer.
    parser.add_argument("--real-hostname", action="store_true", default=False, help="Give the application the hostname")

    # Passthrough locale information.
    parser.add_argument("--locale", action="store_true", default=False, help="Give the application locale information")

    # Passthrough spellchecking via hunspell.
    parser.add_argument("--hunspell", action="store_true", default=False, help="Give the application dictionaries")

    # Give the application git.
    parser.add_argument("--git", action="store_true", default=False, help="Give the application git")

    # Use hardened malloc
    parser.add_argument("--hardened-malloc", action="store_true", default=False, help="Use hardened_malloc within the sandbox")

    # Make a desktop entry for this sandboxed application.
    parser.add_argument("--make-desktop-entry", action="store_true", default=False, help="Create a desktop entry for the application.")

    # If the desktop entry for the application does not follow the application.desktop convention, explicitly specify the
    # name of the desktop entry as found in /usr/share/applications.
    parser.add_argument("--desktop-entry", action="store", default=None, help="The application's desktop entry, if it cannot be deduced automatically. For example, chromium.desktop")

    # Be verbose in logging.
    parser.add_argument("--verbose", action="store_true", default=False, help="Verbose logging")

    # Dry and Startup related settings.
    # --dry will do everything except run the program, such as createing the SOF folder.
    # --startup should only be used by the systemd startup service, to let the program know that it's not being run by the user.
    # --dry-startup will let the startup service know that this application should be dry-run on boot, so that SOF folder's
    # can be generated immediately.
    parser.add_argument("--dry", action="store_true", default=False, help="Dry run a program, establishing the SOF folder, but don't run it.")
    parser.add_argument("--startup", action="store_true", default=False, help="DO NOT USE.")
    parser.add_argument("--dry-startup", action="store_true", default=False, help="Tell the systemd startup script that this program should be run on start.")

    arguments, unknown = parser.parse_known_args()
    arguments.unknown = unknown
    arguments = vars(arguments)

    # SB.conf contains keypairs separated by =.
    # The key is a case insensitive matching of anything above.
    # The value is not enforced, users are expected to know better.
    config = Path(environ["XDG_CONFIG_HOME"], "sb", "sb.conf")
    config.parent.mkdir(parents=True, exist_ok=True)
    if config.is_file():
        for line in config.open("r").readlines():
            try:
                key, value = line.split("=")
                key = key.lower()
                value = value.strip("\n")
                if key in arguments and not any(arg.startswith(f"--{key}") for arg in argv):
                    arguments[key] = value
            except Exception as e:
                print("Invalid configuration:", line, e)
    return arguments
