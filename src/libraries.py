"""
@brief This file contains library lookup and cache generation.
@info SB has two types of caches pertaining to libraries. The first
is the lib.cache stored for each application that contains both individual .so
libraries, and also folders, within the /usr/lib directory (Which is symlinked to
/lib). This cache contains all the contents of /usr/lib that the application needs
to run, and is determined by recursively calling ldd on all binaries the application
needs. This can be updated with --update-libraries, and is a fast operation
@info The second cache is the folder cache that determines libraries OUTSIDE of a
folder in /usr/lib is needed for those libraries within it; for example:
ldd /usr/lib/qt6/bin/qtpaths will return a dependency on /usr/lib/libcap.so.2
We don't want to hardlink each file in the directory, so we instead bind mount
the whole folder, and add any individual libraries that the folder might need. As
the example suggests, this is entirely due to the /usr/lib/qt{5,6,} folders. Unlike
--update-libraries, the corresponder updator --update-library-cache is very slow, hence
why we've created a second cache to abstract from the main application cache. Now, applications
can simply read a cache file containing all the libraries needed for a certain folder. sb-refresh
will delete both caches.
"""

from pathlib import Path

from shared import args, output, log, data


# The set of libraries the application currently needs.
current = set()


# Used for library dependency lookup. Libraries will contain all the needed libraries,
# Searched will contain libraries that have already been searched.
searched = {""}


# A set of wildcard libraries, so that we can con
wildcards = set()


def get(to_load, current=set()):
  """
  @brief Get the current libraries for a binary.
  @param to_load: The binary or library
  @pararm current: The list of current libraries we should add to
  """

  global searched, wildcards

  # If the binary/library has already been searched, just return, otherwise add it.
  if to_load in searched or to_load == "":
    return
  searched.add(to_load)

  # If its a directory, then use the directory cache.
  if Path(to_load).is_dir():
    cache = Path(data, "sb", "cache")
    cache.mkdir(parents=True, exist_ok=True)

    sub = to_load.replace("/", ".")
    if not sub.endswith("."):
      sub += "."
    sub += "cache"

    dir_cache = cache / sub

    if not dir_cache.is_file() or args.update_cache:
      local_libraries = output(["sb-cache", str(to_load)])
      if local_libraries:
        with dir_cache.open("w") as file:
          for library in local_libraries[:-1]:
            if library != "" and library != "not":
              file.write(f"{library} ")
          file.write(local_libraries[-1])

    if dir_cache.is_file():
      libs = set(dir_cache.open("r").readline().strip().split(" "))
      current |= libs
      current.add(to_load)
    return

  # If there's a wildcard, expand it.
  if "*" in to_load:
    wildcards |= to_load
    return

  # Otherwise, add the library/binary to libraries, and then add all libraries needed via ldd.
  current |= {to_load}
  for library in output(["ldd", to_load]):
    split = library.split()

    # Handle the esoteric syntax of ldd.
    lib = None
    if len(split) > 0 and split[0].startswith("/usr/lib"):
      lib = split[0]
    elif len(split) > 2 and split[2] != "not":
      lib = split[2]
      lib = lib.replace("lib64", "lib")
      if lib.startswith("/lib"):
        lib = lib[4:]

    # If we have a library, check it as well.
    if lib is not None:
      get(lib, current)
  return


def setup(sof_dir, lib_cache, update_sof):
  """
  @brief Setup the library folder.
  @param sof_dir: Where the SOF dir is located.
  @param lib_cache: Where the library cache is.
  @param update_sof: Whether we should actually update the SOF if it is already present.
  """

  global current

  # If we are explicitly told to update the libraries, or the SOF dir doesn't exist, then update.
  if args.update_libraries or not sof_dir.is_dir():

    # Make the directory if it doesn't exist.
    if not sof_dir.is_dir():
      sof_dir.mkdir(parents=True, exist_ok=True)

    # If we want to update the SOF, perform the recursive library dependency check.
    if update_sof:

      log("Finding wildcard libraries...")
      command = ["find", "/usr/lib", "-mindepth", "1", "-maxdepth", "1", "-executable"]
      for library in wildcards:
        command.extend(["-name", library[library.rfind("/") + 1:], "-o"])
      # Remove the trailing -o
      command = command[:-1]
      current |= set(output(command))

      # We simply get libraries based on everything within libraries that hasn't been searched.
      # Run this however many times it takes before all dependencies are found.
      log("Determining library dependencies...")
      unsearched = current - searched
      while unsearched:
        for lib in unsearched:
          get(lib, current)
        unsearched = current - searched

    # When updating, we need to overwrite the lib_cache.
    with lib_cache.open("w") as cache:
      for lib in current:
        cache.write(lib)
        cache.write(" ")

    # Now, create our library folder, by copying everything in libraries into the folder.
    # We use a SHARED folder, and then create hard-links off of that, to reduce duplication
    # between multiple applications running under SB.
    log("Creating /lib...")

    # Create the shared runtime.
    runtime_lib = Path("/tmp", "sb", "shared")
    runtime_lib.mkdir(parents=True, exist_ok=True)

    # Create the application-specific folder.
    sof_dir.mkdir(parents=True, exist_ok=True)

    # For each library, add it to the shared runtime.
    for library in current:

      # Fix some issues that sometimes happen with find.
      if not library.startswith("/usr/lib") and not library.startswith("/usr/bin"):
        log(f"Ignoring invalid library: {library}")
        continue

      # Get the path to the library, and where it should end up in the shared runtime.
      real_path = Path(library)
      runtime_path = Path(f"{runtime_lib}/{library}")

      # If the library isn't a directory, then write it.
      if not real_path.is_dir():
        write(library, runtime_path, real_path, sof_dir)

      # If it IS a directory, then get all the files, and writen them all in.
      elif real_path.is_dir():
        for sub in output(["find", str(real_path), "-type", "f"]):
          if sub:
            write(sub, Path(f"{runtime_lib}/{sub}"), Path(sub), sof_dir)


def write(library, runtime_path, real_path, sof_dir):
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
      log("Permission Denied: ", str(real_path))
      return
    except FileNotFoundError:
      log("File not found:", str(real_path), "This is fine")
      return

  # If we are dealing with files, not folders, Then hardlink the shared library into the SOF.
  if runtime_path.is_file() and real_path.is_file():
    lib_path = Path(f"{str(sof_dir)}/{library}")
    if runtime_path.is_file() and not lib_path.is_file():
      try:
        lib_path.parents[0].mkdir(parents=True, exist_ok=True)
        lib_path.hardlink_to(runtime_path)
        lib_path.chmod(real_path.stat().st_mode)
      except Exception:
        pass