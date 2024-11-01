#!/bin/python



from subprocess import run, Popen, PIPE
from os import environ
from time import sleep
from pathlib import Path
from tempfile import TemporaryDirectory, NamedTemporaryFile
from sys import argv
from hashlib import new

import arguments
import re

# Get the XDG directories.
runtime = environ["XDG_RUNTIME_DIR"]
config = environ["XDG_CONFIG_HOME"]
cache = environ["XDG_CACHE_HOME"]
data = environ["XDG_DATA_HOME"]
home = environ["HOME"]

# Used for library dependency lookup. Libraries will contain all the needed libraries,
# Searched will contain libraries that have already been searched.
searched = set()
searched.add("")

libraries = set()

args = arguments.parse()


def desktop_entry():
  """
  @brief Make a desktop entry for the sandbox.
  """

  # Firstly, remove this part of the command so we don't write it into the executable.
  exec = argv
  exec.remove("--make-desktop-entry")

  # Pop twice to remove it and the name itself.
  if args.desktop_entry:
    i = exec.index("--desktop-entry")
    exec.pop(i); exec.pop(i)
  exec.insert(2, '"$@"')

  # Get the name, and setup the buffer and binary.
  name = args.desktop_entry if args.desktop_entry else f"{args.program}.desktop"
  exec = " ".join(exec)
  binary = f"{home}/.local/bin/{name}.sb"
  buffer = ""

  # Read the original application file, but replace the Exec Line with the path to the sandbox binary.
  with open(f"/usr/share/applications/{name}", "r") as f:
    for line in f.readlines():
      if line.startswith("Exec="):
        buffer += (f"Exec={binary} {" ".join(line.split()[1:])}\n")
      else:
        buffer += line

  # Actually write the sandbox binary.
  with open(binary, "w") as b:
    b.write("#!/bin/sh\n")
    b.write(f"{exec}")
  run(["chmod", "+x", binary])

  # Write the sandboxed desktop entry to the user's application folder.
  with open(f"{data}/applications/{name}", "w") as f:
    f.write(buffer)
  return


def main():
  global libraries

  # If we are making a desktop entry, or on startup and it's not a startup app, do the action and return.
  if args.make_desktop_entry: desktop_entry()
  if args.startup and not args.dry_startup: return

  # Get the basename of the program.
  path = args.program
  if path.startswith("/"):
    program = path.split("/")[-1]
  else:
    program = path

  # Setup the .flatpak info values.
  application_name=f"app.application.{program}"

  # Create the application's runtime folder
  application_folder=Path(runtime, "app", application_name)
  application_folder.mkdir(parents=True, exist_ok=True)

  # The .flatpak-info is just a temporary file, but the application's temporary directory is a seperate TMPFS,
  # separate from the global /tmp.
  temp = TemporaryDirectory()
  info = NamedTemporaryFile()

  if args.verbose: print(f"Running {application_name}")
  if args.verbose: print(f"\tTemp: {temp}")

  # Write the .flatpak-info file so the application thinks its running in flatpak and thus portals work.
  info.write(b"[Application]\n")
  name_str=f"name={application_name}"
  info.write(name_str.encode())
  info.flush()

  # Setup the DBux Proxy. This is needed to mediate Dbus calls, and enable Portals.
  if args.verbose: print("Launching D-Bus Proxy")
  dbus_proxy(args.portals, application_folder, info.name)

  # We need to wait a slight amount of time for the Proxy to get up and running.
  sleep(0.01)

  # Then, run the program.
  if args.verbose:
    print("Launching ", program, "at", path)
  run_application(program, path, application_folder, info.name, temp.name)


# Run a command, put the results in a list.
def output(command):
  process = run(command, stdout=PIPE, stderr=PIPE)
  if args.verbose:
    errors = [out for out in process.stderr.decode().split("\n") if out]
    if errors: print(errors)

  return [out for out in process.stdout.decode().split("\n") if out]


# Find libraries in /usr/lib with wildcard support.
def find_libs(expression, depth=1, path="/usr/lib"):
  if "/" in expression:
    base = expression.rfind("/")
    path += "/" + expression[:base]
    expression = expression[base + 1:]
  command = [
    "find", f"{path}/",
    "-maxdepth", str(depth), "-mindepth", str(1),
    "-executable",
    "-name", f'{expression}'
  ]
  libs = set(output(command))
  return libs


def get_libraries(to_load, libraries=set()):
  """
  @brief Get the needed libraries for a binary.
  @param to_load: The binary or library
  @pararm libraries: The list of libraries we should add to
  @param recursive_folders: Whether to search folders.
  """

  global searched

  # If the binary/library has already been searched, just return, otherwise add it.
  if to_load in searched or to_load == "": return
  searched.add(to_load)

  # If its a directory, then either search if the user wants, or just be content having added itself to searched.
  if Path(to_load).is_dir():
    cache = Path(data, "sb", "cache")
    cache.mkdir(parents=True, exist_ok=True)

    sub = to_load.replace("/", ".")
    if not sub.endswith("."): sub += "."
    sub += "cache"

    dir_cache = cache / sub

    if not dir_cache.is_file() or args.update_library_cache:
      local_libraries = output(["sb-cache", str(to_load)])
      if local_libraries:
        with dir_cache.open("w") as file:
          for library in local_libraries[:-1]:
            if library != "" and library != "not":
              file.write(f"{library} ")
          file.write(local_libraries[-1])

    if dir_cache.is_file():
      libs = set(dir_cache.open("r").readline().strip().split(" "))
      libraries |= libs
      libraries.add(to_load)
    return

  # If there's a wildcard, expand it.
  if "*" in to_load:
    libraries |= find_libs(to_load, path="/usr/lib")
    return

  # Otherwise, add the library/binary to libraries, and then add all libraries needed via ldd.
  libraries |= {to_load}
  for library in output(["ldd", to_load]):
    split = library.split()

    # Handle the esoteric syntax of ldd.
    lib = None
    if len(split) > 0 and split[0].startswith("/usr/lib"):
      lib = split[0]
    elif len(split) > 2 and split[2] != "not":
      lib = split[2]
      lib = lib.replace("lib64", "lib")
      if lib.startswith("/lib"): lib = lib[4:]

    # If we have a library, check it as well.
    if lib is not None: get_libraries(lib, libraries)
  return


def setup_lib(sof_dir, lib_cache, update_sof):
  """
  @brief Setup the library folder.
  @param args: The program arguments.
  @param sof_dir: Where the SOF dir is lcoated.
  @param lib_cache: Where the library cache is.
  @param update_sof: Whether we should actually update the SOF if it is already present.
  """

  # If we are explicitly told to update the libraries, or the SOF dir doesn't exist, then update.
  if args.update_libraries or not sof_dir.is_dir():

    # Make the directory if it doesn't exist.
    if not sof_dir.is_dir(): sof_dir.mkdir(parents=True, exist_ok=True)

    # If we want to update the SOF, perform the recursive library dependency check.
    if update_sof:
      if args.verbose: print("Determining library dependencies...")

      # We simply get libraries based on everything within libraries that hasn't been searched.
      # Run this however many times it takes before all dependencies are found.
      unsearched = libraries - searched
      while unsearched:
        for lib in unsearched:
          get_libraries(lib, libraries)
        unsearched = libraries - searched

    # When updating, we need to overwrite the lib_cache.
    with lib_cache.open("w") as cache:
      for lib in libraries:
        cache.write(lib)
        cache.write(" ")

    # Now, create our library folder, by copying everything in libraries into the folder.
    # We use a SHARED folder, and then create hard-links off of that, to reduce duplication
    # between multiple applications running under SB.
    if args.verbose: print("Creating /lib...")

    # Create the shared runtime.
    runtime_lib = Path("/tmp", "sb", "shared")
    runtime_lib.mkdir(parents=True, exist_ok=True)

    # Create the application-specific folder.
    sof_dir.mkdir(parents=True, exist_ok=True)

    # For each library, add it to the shared runtime.
    for library in libraries:

      # Fix some issues that sometimes happen with find.
      if not library.startswith("/usr/lib") and not library.startswith("/usr/bin"):
        if args.verbose: print(f"Ignoring invalid library: {library}")
        continue

      # Get the path to the library, and where it should end up in the shared runtime.
      real_path = Path(library)
      runtime_path = Path(f"{runtime_lib}/{library}")

      # If the library isn't a directory, then write it.
      if not real_path.is_dir():
        write_to_cache(library, runtime_path, real_path, sof_dir)

      # If it IS a directory, then get all the files, and writen them all in.
      elif real_path.is_dir():
        for sub in output(["find", real_path, "-type", "f"]):
          if sub: write_to_cache(sub, Path(f"{runtime_lib}/{sub}"), Path(sub), sof_dir)


def write_to_cache(library, runtime_path, real_path, sof_dir):
  """
  @brief Write a file to the SOF library folder.
  @param library: The name of the library.
  @param runtime_path: Where the shared library folder is.
  @param real_path: Where the path is on the actual system.
  @param sof_dir: The path to the program's SOF.
  """

  # If the library isn't in the shared folder, then add it.
  if not runtime_path.is_file():

    # Make any needed directories.
    runtime_path.parents[0].mkdir(parents=True, exist_ok=True)

    # Try to read the library, and dump it into the runtime.
    # Then, set the correct CHMOD.
    try:
      runtime_path.open("wb").write(real_path.open("rb").read())
      runtime_path.chmod(real_path.stat().st_mode)

    # Handle exceptions, but we don't really care.
    except PermissionError:
      print("Permission Denied: ", str(real_path))
      return
    except FileNotFoundError:
      print("File not found:", str(real_path), "This is fine")
      return

  # If we are dealing with files, not folders, Then hardlink the shared library into the SOF.
  if runtime_path.is_file() and real_path.is_file():
    lib_path = Path(f"{str(sof_dir)}/{library}")
    if runtime_path.is_file() and not lib_path.is_file():
      try:
        lib_path.parents[0].mkdir(parents=True, exist_ok=True)
        lib_path.hardlink_to(runtime_path)
        lib_path.chmod(real_path.stat().st_mode)
      except Exception: pass


# Share a list of files under a specified mode.
def share(command: list, paths: list, mode = "ro-bind-try"):
  for path in paths:
    if Path(path).is_symlink():
      true = str(Path(path).readlink())
      command.extend([f"--{mode}", true, true])
      command.extend(["--symlink", true, path])
    else:
      command.extend([f"--{mode}", path, path])


# Run the DBUS Proxy.
def dbus_proxy(portals, application_folder, info_name):
  command = ["bwrap"]
  command.extend([
    "--new-session",
    "--symlink", "/usr/lib64", "/lib64",
    "--ro-bind", "/usr/lib", "/usr/lib",
    "--ro-bind", "/usr/lib64", "/usr/lib64",
    "--ro-bind", "/usr/bin", "/usr/bin",
    "--clearenv",
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
  command.extend([f'--see={portal}' for portal in args.see])
  command.extend([f'--talk={portal}' for portal in args.talk])
  command.extend([f'--own={portal}' for portal in args.own])

  if args.xdg_open: command.extend(["--talk=org.freedesktop.portal.OpenURI"])

  if args.verbose:
    print(" ".join(command))
    command.extend(["--log"])
  Popen(command)


# Run the application.
def run_application(application, application_path, application_folder, info_name, temp):
  command = ["bwrap", "--new-session", "--die-with-parent"]
  sof_dir =  Path("/tmp", "sb", application)
  local_dir =  Path(data, "sb", application)
  if not local_dir.is_dir(): local_dir.mkdir(parents=True)

  # The lib cache contains a list of all needed libraries,
  # The command cache contains the entire bwrap command, given the hash of the sb command/
  lib_cache = Path(local_dir, "lib.cache")
  cmd_cache = Path(local_dir, "cmd.cache")

  # Add the flatpak-info.
  command.extend(["--ro-bind-try", info_name, f"{runtime}/flatpak-info"])
  command.extend(["--ro-bind-try", info_name, "/.flatpak-info"])

  # If we have a home directory, add it.
  if args.home:
    home_dir = Path(local_dir, "home")
    home_dir.mkdir(parents=True, exist_ok=True)

    # If it's cached, make a copy on a TMPFS.
    if args.cached_home:
      cache_home = TemporaryDirectory()
      run(["cp", "-r", str(home_dir) + "/.", cache_home.name])
      command.extend(["--bind", cache_home.name, "/home"])
    else:
      command.extend(["--bind", str(home_dir), "/home"])

  if args.share_cache: command.extend(["--bind", f"{home}/.cache", f"{home}/.cache"])
  else: command.extend(["--bind", temp, f"{home}/.cache"])

  # Add the tmpfs.
  command.extend(["--tmpfs", temp])

  # Get the cached portion of the command.
  command.extend(gen_command(application, application_path, application_folder))

  # Handle unknown arguments. If they're files, deal with them, otherwise
  enclave_contents = {}
  file_enclave = None
  if not args.do_not_allow_file_passthrough:
    for argument in args.unknown:

      # Try and treat the argument as a file, or a directory.
      path = Path(argument)

      # If a directory, we just bind mount it.
      if path.is_dir(): share(command, [argument], mode = "bind-try" if args.file_passthrough_rw else "ro-bind-try")

      # If a file, we may do an enclave depending on the user.
      elif path.is_file():
        if args.file_enclave:
          if file_enclave is None: file_enclave = TemporaryDirectory()
          command.extend(["--bind", file_enclave.name, "/enclave"])
        else: share(command, [argument], mode = "bind-try" if args.file_passthrough_rw else "ro-bind-try")


  # Either launch the debug shell, run under strace, or run the command accordingly.
  if args.debug_shell: command.append("sh")
  else:
    if args.strace: command.extend(["strace", "-ff"])

    # Zypak must be run on the direct binary, so if we're passed
    # a shell script, parse it
    if args.zypak:
      command.extend(["zypak-wrapper"])

      binary = output(["which", application_path])[0]
      application = [application_path]

      # If we get an exception, it's a binary.
      try:
        with open(binary) as file:
          content = file.readlines()
          for line in content:
            # Get the line with electron, and split the "electron asar" pair.
            if "electron" in line:
              split = line.split()
              for x in range(len(split)):
                if "electron" in split[x]:
                  application = split[x:]
                  if "/usr/lib" not in application[0]:
                    application[0] = f"/usr/lib/{application[0]}/electron"
                  break
      except UnicodeDecodeError: pass
      command.extend(application)
    else: command.append(application_path)

  # Now, we tack on all the unknown arguments.
  for argument in args.unknown:
    file = Path(argument)

    # If it's a file, we move it to the enclave if needed, otherwise just append it.
    if file.is_file():
      if file_enclave is not None:
        enclave_file = Path(file_enclave.name, file.name)
        if args.verbose: print(f"Adding {argument} to file enclave")
        enclave_contents[enclave_file.resolve()] = file.resolve()
        enclave_file.open("wb").write(file.open("rb").read())

        run(["chmod", "666", enclave_file.resolve()])
        command.append(f"/enclave/{file.name}")
      else: command.append(argument)

    # Everything else is just appended.
    else: command.append(argument)

  # So long as we aren't dry-running, run the program.
  if not args.dry:
    if args.verbose: print("Command:", " ".join(command))
    run(command)

  # If we have RW acess, and there's things in the enclave, update the source.
  if args.file_passthrough_rw:
    for file, dest in enclave_contents.items():
      if args.verbose: print(f"Updating {dest} from file enclave")
      Path(dest).open("wb").write(Path(file).open("rb").read())


# Generate the cacheable part of the command.
def gen_command(application, application_path, application_folder):
  global libraries, searched

  # Get all our locations.
  sof_dir =  Path("/tmp", "sb", application)
  local_dir =  Path(data, "sb", application)
  lib_cache = Path(local_dir, "lib.cache")
  cmd_cache = Path(local_dir, "cmd.cache")

  # Generate a hash of our command.
  h = new("SHA256")
  h.update("".join(argv).encode())
  hash = h.hexdigest()

  # If we don't explicitly want to update the libraries, the SOF exists, there is a library cache,
  # and the hash matches the command hash, just return the cached copy of both.
  update_sof = args.update_libraries
  if update_sof is False:
    if not sof_dir.is_dir():
      if lib_cache.is_file():
        if args.verbose: print("Using cached library definitions")
        libraries = set([library for library in lib_cache.open("r").read().split(" ")])
        if args.verbose: print(libraries)
      else:
        update_sof = True

  if cmd_cache.is_file():
    with cmd_cache.open("r") as file:
      info = file.read().split("\n")
      cached = info[0]
      if hash == cached and sof_dir.is_dir() and not args.update_libraries:
        if args.verbose: print("Using cached command")
        return info[1].split(" ")


  # Get the basic stuff.
  if args.verbose: print("Setting up...")
  command = []
  command.extend([
    "--dir", runtime,
    "--chmod", "0700", runtime,
    "--ro-bind-try", f"{application_folder}/bus", f"{runtime}/bus",
    "--bind-try", f"{runtime}/doc", f"{runtime}/doc",
  ])
  share(command, ["/run/dbus"])

  if args.lib: share(command, ["/usr/lib"])
  else: command.extend(["--dir", "/usr/lib"])

  # Symlink lib (In the sandbox, not to the host)
  share(command, ["/etc/ld.so.preload", "/etc/ld.so.cache"])
  command.extend([
    "--symlink", "/usr/lib", "/lib",
    "--symlink", "/usr/lib", "/lib64",
    "--symlink", "/usr/lib", "/usr/lib64",
  ])

  if args.xdg_open: args.binaries.extend(["xdg-open"])

  if args.zypak:
    args.binaries.extend([
      "zypak-helper",
      "zypak-sandbox",
      "zypak-wrapper",
      "zypak-wrapper.sh",
    ])
    if update_sof: libraries |= find_libs("libzypak*")

  # Add python, if needed.
  if args.python:
    version = f"python{args.python}"
    args.binaries.extend([version, "python"])
    if update_sof:
      libraries |= {
        f"lib{version}.so",
        f"/usr/lib/{version}/"
      }

  # Git
  if args.git:
    share(command, ["/dev/null"], "dev-bind")
    args.binaries.extend([
      "git", "git-cvsserver", "git-receive-pack", "git-shell",
      "git-upload-archive", "git-upload-pack", "gitk", "scalar"
    ])
    args.libraries.append("/usr/lib/git-core/")
    share(command, ["/usr/share/git/"])


  if args.zsh:
    args.ro.extend([
      "/etc/shells", "/usr/share/zsh/", "/etc/zsh/",
      "/etc/profile", "/usr/share/terminfo", "/var/run/utmp",
      "/etc/group",
      f"{home}/.zshrc", f"{config}/environment.d",
    ])
    args.devices.extend(["/dev/pts/"])
    args.proc = True

    # For shell and coredump.
    args.binaries.extend(["zsh", "mv"])
    libraries |= {"/usr/lib/zsh/"}


  if args.include:
    args.ro.extend([
      "/usr/include",
      "/usr/local/include"
    ])
    libraries |= {"/usr/lib/clang", "/usr/lib/gcc"}

  # Add electron
  if args.electron:
    if args.verbose: print("Adding electron...")

    # NSS junk
    if update_sof:
      libraries |= find_libs("libsoftokn3*")
      libraries |= find_libs("libfreeblpriv3*")
      libraries |= find_libs("libsensors*")
      libraries |= find_libs("libnssckbi*")

    share(command, ["/sys/block", "/sys/dev"])
    share(command, ["/dev/null", "/dev/urandom", "/dev/shm"], "dev-bind")

    if args.electron_version:
      # Extend existing args so they can be handled.
      if args.electron_version == "stable":
        electron_string = f"electron{environ["ELECTRON_VERSION"]}"
      else:
        electron_string = f"electron{args.electron_version}"

      args.binaries.extend([
        f"/usr/lib/{electron_string}/electron",
        f"/usr/bin/{electron_string}",

      ])
      args.ro.append(f"/usr/lib/{electron_string}")

      if update_sof:
        libraries |= find_libs("*", path=f"/usr/lib/{electron_string}")

    # Enable needed features.
    args.dri = True
    args.gtk = True
    if not args.proc:
      if args.verbose: print("/proc is needed for electron applications. Enabling...")
      args.proc = True

  # Add KDE
  if args.kde:
    if args.verbose: print("Adding KDE...")
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
      libraries |= find_libs("libKF*")
      libraries |= find_libs("lib*Kirigami*")
      libraries |= {"/usr/lib/kf6/"}

    args.dri = True
    args.qt = True


  # Add QT
  if args.qt or args.qt5:
    if args.verbose: print("Adding QT...")
    share(command, ["/usr/share/qt6"])
    if args.qt5: share(command, ["/usr/share/qt5"])
    if update_sof:
      libraries |= {"/usr/lib/qt6/"}
      libraries |= find_libs("libQt*")
      if args.qt5: libraries |= {"/usr/lib/qt5/", "/usr/lib/qt/"}
    args.dri = True

  # Add GTK
  if args.gtk:
    if args.verbose: print("Adding GTK...")
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
      libraries |= find_libs("libgtk*")
      libraries |= find_libs("libgdk*")
      libraries |= find_libs("libgio*")
      libraries |= {"/usr/lib/gdk-pixbuf-2.0/", "/usr/lib/gtk-3.0"}
    args.dri = True


  # Mount devices, or the dev
  if args.dev:
    command.extend(["--dev", "/dev"])
  else:
    for device in args.devices: share(command, [device], "dev-bind-try")

  # Mount system directories.
  if args.proc: command.extend(["--proc", "/proc"])
  if args.etc: share(command, ["/etc"])
  if args.usr_share: share(command, ["/usr/share"])
  if args.sys: share(command, ["/sys"], "dev-bind-try")

  # Add DRI stuff.
  if args.dri:
    if args.verbose: print("Adding DRI...")
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
        f"{config}/fontconfig", f"{data}/fontconfig", f"{cache}/fontconfig"
        "/usr/share/themes", "/usr/share/color-schemes", "/usr/share/icons", "/usr/share/cursors", "/usr/share/pixmaps",
        "/usr/share/mime",
        f"{data}/mime",
        f"{data}/pixmaps",
      ])
    if "XDG_SESSION_DESKTOP" in environ: command.extend(["--setenv", "XDG_SESSION_DESKTOP", environ["XDG_SESSION_DESKTOP"]])
    if update_sof:
      for lib in [
        "libvulkan*", "libglapi*", "*mesa*", "*Mesa*", "libdrm", "libGLX*", "libEGL*",
        "libVkLayer*", "libgbm*", "libva*", "*egl*", "*EGL*"
        ]:
          libraries |= find_libs(lib)
      libraries |= find_libs("*", path="/usr/lib/dri")
      libraries |= find_libs("*", path="/usr/lib/gbm")

  # Add the wayland socket and XKB
  if "wayland" in args.sockets:
    share(command, [
      f"{runtime}/wayland-0",
      "/usr/share/X11/xkb",
      "/etc/xkb"
    ])

  # Add the pipewire socket, and its libraries.
  if "pipewire" in args.sockets:
    share(command, [
      f"{runtime}/pipewire-0",
      f"{runtime}/pulse",
      f"{config}/pulse"
      "/etc/pipewire",
      "/usr/share/pipewire",
    ])
    if update_sof:
      libraries |= find_libs("libpipewire*")
      libraries |= {"/usr/lib/pipewire-0.3/", "/usr/lib/spa-0.2/"}

  # add Xorg. This is a vulnerability.
  if "xorg" in args.sockets:
    if "DISPLAY" in environ: command.extend(["--setenv", "DISPLAY", environ["DISPLAY"]])
    share(command, ["/tmp/.X11-unix/X0"])
    share(command, output(["find", runtime, "-maxdepth", "1", "-name", "xauth_*"]))

  # Extend the hostname if needed, otherwise use a fake one.
  if args.real_hostname: share(command, ["/etc/hostname"])
  else: command.extend(["--hostname", "sandbox"])

  # Add locale information
  if args.locale:
    share(command, [
      "/etc/locale.conf",
      "/etc/localtime",
      "/usr/share/zoneinfo",
      "/usr/share/X11/locale",
      "/usr/share/locale",
      f"{config}/plasma-localerc"
    ])
    if update_sof: args.ro.append("/usr/lib/locale/")
    command.extend([
      "--setenv", "LANG", environ["LANG"],
      "--setenv", "LANGUAGE", environ["LANGUAGE"],
    ])
    args.binaries.append("locale")

  # Hunspell
  if args.hunspell: share(command, ["/usr/share/hunspell", "/usr/share/myspell"])

  # Handle sharing namespaces.
  if args.share == "none": command.append("--unshare-all")
  elif args.share != "all":
    for shared in ["user", "ipc", "pid", "net", "uts", "cgroup"]:
      if shared not in args.share:
        command.append(f"--unshare-{shared}")

  # Networking adds the networking stack so it works properly.
  if "net" in args.share:
    if "--unshare-all" in command:
      command.append("--share-net")
    share(command, [
      "/etc/gai.conf", "/etc/hosts.conf", "/etc/hosts", "/etc/host.conf", "/etc/nsswitch.conf", "/etc/resolv.conf", "/etc/gnutls/config",
      "/etc/ca-certificates", "/usr/share/ca-certificates/",
      "/etc/pki", "/usr/share/pki",
      "/etc/ssl", "/usr/share/ssl",
    ])
    if update_sof:
      libraries |= find_libs("libnss*")

  # Add variables
  for variable in args.env:
    split = variable.split("=")
    command.extend(["--setenv", split[0], "=".join(split[1:])])


  # Add binaries.
  if args.bin:
    if args.verbose: print("Adding binaries...")
    command.extend(["--ro-bind-try", "/usr/bin", "/usr/bin"])
  else:
    if args.verbose: print("Generating binaries...")
    binaries = args.binaries
    binaries.append(application_path)
    if args.debug_shell: binaries.append("sh")
    if args.strace: binaries.append("strace")

    # Get builtins and reserved words
    builtins = output(["bash", "-c", "compgen -bk"])

    def add_binary(binary):
      if binary.startswith("/"):

        # /bin, /sbin and /usr/sbin are all symlinks, so we can't actually
        # put files there.
        if binary.startswith(("/bin/", "/sbin/")): binary = f"/usr{binary}"
        if binary.startswith("/usr/sbin"): binary.replace("sbin", "bin")

        share(command, [binary], "ro-bind-try")
        path = binary
        binary = path.split("/")[-1]
      else:
        path = output(["which", binary])[0]
        share(command, [path], "ro-bind-try")
      if update_sof: get_libraries(path, libraries)

      # Parse shell scripts
      try:
        with open(path) as binary_file:
          shebang = binary_file.readline().strip(" \n")
          if not shebang.startswith("#"): return

          # This helps for cases like #!/usr/bin/env bash
          for shell in shebang[2:].split(" "): add_binary(shell)

          here_doc = None
          for line in binary_file:
            stripped = line.strip()

            # comments, bash syntax, functions
            if not stripped or stripped.startswith("#"): continue

            if "<<" in stripped:
              split = stripped.split(" ")
              here_doc = split[split.index("<<") + 1].strip("'\"")
              continue
            if here_doc:
              if here_doc in split:
                here_doc = None
              else: continue

            # Tokenize
            for split in re.split(r'\s|(?<=\(|\)|\$|\'|\")', stripped):
              if  len(split) <= 1 \
                  or split == binary \
                  or split in builtins \
                  or split.startswith(("-", "\"", "'", "{", "}")) \
                  or any(x in split for x in ["=", "&", "|", "(", ")", "/", "\"", "'", "[", "]"]):
                    continue
              try: add_binary(split)
              except Exception: pass

      # Don't try and handle compiled binaries.
      except UnicodeDecodeError: pass

    for binary in binaries: add_binary(binary)
  command.extend(["--symlink", "/usr/bin", "/bin"])
  command.extend(["--symlink", "/usr/bin", "/bin"])
  command.extend(["--symlink", "/usr/bin", "/sbin"])
  command.extend(["--symlink", "/usr/bin", "/usr/sbin"])


  # If we aren't just passing through /lib, setup our SOF.
  if update_sof:
    for library in args.libraries:
      get_libraries(library, libraries)

  # If we're updating the SOF, generate all the libraries needed based on those that were explicitly provided.
  if not args.lib:
    setup_lib(sof_dir, lib_cache, update_sof)
    command.extend(["--bind", f"{str(sof_dir)}/usr/lib", "/usr/lib"])

  # Setup application directories.
  if "config" in args.app_dirs: share(command, [f"{config}/{application}"], "bind-try")
  if "cache" in args.app_dirs: share(command, [f"{cache}/{application}"], "bind-try")
  if "share" in args.app_dirs: share(command, [f"/usr/share/{application}"], "bind-try")
  if "data" in args.app_dirs: share(command, [f"{data}/{application}"], "bind-try")
  if "etc" in args.app_dirs: share(command, [f"/etc/{application}"], "bind-try")
  if "lib" in args.app_dirs: share(command, [f"/usr/lib/{application}"])

  # Setup any paths explicitly provided.
  for path in args.rw: share(command, [path], "bind-try")
  for path in args.ro: share(command, [path])

  # Write the command cache.
  with cmd_cache.open("w") as file:
    file.write(hash)
    file.write("\n")
    file.write(" ".join(command))

  return command


if __name__ == "__main__": main()
