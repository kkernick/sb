from subprocess import run, PIPE
from os import environ
from pathlib import Path

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


def resolve(path):
    if ":" in path:
        s = path.split(":")
        return s[0], s[1]
    else:
        return path, path


# Share a list of files under a specified mode.
def share(command: list, paths: list, mode = "ro-bind-try"):
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
        else:
            command.extend([f"--{mode}", path, dest])


def env(variable):
    if  variable in environ:
        return ["--setenv", variable, environ[variable]]
    return []


nobody = output(["id", "-u", "nobody"])[0]
real = output(["id", "-u"])[0]
