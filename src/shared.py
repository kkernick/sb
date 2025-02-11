from subprocess import run, PIPE
from os import environ
from pathlib import Path
from time import time

import arguments


# Parse command line arguments
args = arguments.parse()


# Paths
runtime = environ["XDG_RUNTIME_DIR"]
config = environ["XDG_CONFIG_HOME"]
cache = environ["XDG_CACHE_HOME"]
data = environ["XDG_DATA_HOME"]
home = environ["HOME"]

session = environ["DBUS_SESSION_BUS_ADDRESS"]

if args["sof"] == "data":
    sof = Path(data, "sb")
elif args["sof"] == "zram":
    sof = Path("/run", "sb")
else:
    sof = Path("/tmp", "sb")
sof.mkdir(parents=True, exist_ok=True)

# A starting time for profiling information
start = None

# Run a command, put the results in a list.
def output(command):
    process = run(command, stdout=PIPE, stderr=PIPE)
    return [out for out in process.stdout.decode().split("\n") if out]


def log(*messages):
    """
    @brief Log messages, but only if verbose printing.
    """
    if args["verbose"]:
        print(*messages)


def profile_start():
    global start
    if args["verbose"]:
        start = time()


def profile(context):
    if args["verbose"]:
        print(f"{context}: {round((time() - start) * 1000, 2)} msec")


def resolve(path):
    if ":" in path:
        s = path.split(":")
        return s[0], s[1]
    else:
        return path, path


# Share a list of files under a specified mode.
def share(command: list, paths: list, mode = "ro-bind"):
    for path in paths:

        path, dest = resolve(path)
        if dest.startswith("/home/"):
            split = dest.split("/")
            dest = f"/{split[1]}/sb/{"/".join(split[3:])}"

        p = Path(path)
        if p.is_symlink():
            true = str(p.readlink())
            if not true.startswith("/"):
                true = f"{p.parent}/{true}"
            command.extend([f"--{mode}", true, dest])
        elif p.exists():
            command.extend([f"--{mode}", path, dest])
        else:
            log("Warning: path does not exist: ", path)


def env(variable):
    if  variable in environ:
        return ["--setenv", variable, environ[variable]]
    return []


nobody = output(["id", "-u", "nobody"])[0]
real = output(["id", "-u"])[0]
