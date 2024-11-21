"""
@brief This file contains utilities and helper functions used exclusively by __main__
"""


from sys import argv
from subprocess import run
from pathlib import Path

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
    path = Path(f"/usr/share/applications/{name}")
    if not path.is_file():
        path = Path(f"{data}/applications/{name}")
        if not path.is_file():
            print("No desktop file could be found, please manually specify the desktop name with --desktop-entry")
            exit(1)
        print("Your application uses a local desktop entry. SB masks applications by creating a copy in the local application folder. To prevent overwriting, SB has created a backup of this entry")
        back = Path(f"{data}/sb/{args["program"]}/{name}")
        path.rename(back)
        path = back

    with path.open("r") as f:
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
