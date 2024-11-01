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


# Run a command, put the results in a list.
def output(command):
  log("Running:", " ".join(command))
  process = run(command, stdout=PIPE, stderr=PIPE)
  errors = [out for out in process.stderr.decode().split("\n") if out]
  if errors:
    log(" ".join(errors))
  return [out for out in process.stdout.decode().split("\n") if out]


def log(*messages):
  """
  @brief Log messages, but only if verbose printing.
  """
  if args.verbose:
    print(*messages)


# Share a list of files under a specified mode.
def share(command: list, paths: list, mode = "ro-bind-try"):
  for path in paths:
    if Path(path).is_symlink():
      true = str(Path(path).readlink())
      command.extend([f"--{mode}", true, true])
      command.extend(["--symlink", true, path])
    else:
      command.extend([f"--{mode}", path, path])
