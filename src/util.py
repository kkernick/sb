"""
@brief This file contains utilities and helper functions used exclusively by __main__
"""


from sys import argv
from subprocess import run

from shared import args, data, home


def desktop_entry():
  """
  @brief Make a desktop entry for the sandbox.
  """

  # Firstly, remove this part of the command so we don't write it into the executable.
  exec = argv
  exec.remove("--make-desktop-entry")

  # Pop twice to remove it and the name itself.
  if args["desktop_entry"]:
    i = exec.index("--desktop-entry")
    exec.pop(i)
    exec.pop(i)
  exec.insert(2, '"$@"')

  # Get the name, and setup the buffer and binary.
  name = args["desktop_entry"] if args["desktop_entry"] else f"{args["program"]}.desktop"
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
  exit(0)
