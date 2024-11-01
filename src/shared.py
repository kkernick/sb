from subprocess import run, PIPE
from os import environ

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
