

from re import split
from pathlib import Path

from shared import args, output, log, data


# Get builtins and reserved words
builtins = output(["bash", "-c", "compgen -bk"])

# A list of current binaries.
current = set()


def add(binary):
    """
    @brief Add the list of binaries associated with the provided.
    @return A set of binaries
    """

    global current
    ret = set()

    # If we have an absolute path, extract.
    if binary.startswith("/"):
        path = binary
        binary = path.split("/")[-1]

    # Otherwise, use which
    else:
        path = output(["which", binary])
        if not path:
            return ret
        path = path[0]

    # /bin, /sbin and /usr/sbin are all symlinks, so we can't actually
    # put files there.
    if path.startswith(("/bin/", "/sbin/")):
        path = f"/usr{path}"
    if path.startswith("/usr/sbin"):
        path.replace("sbin", "bin")
    ret.add(path)

    # Parse shell scripts.
    ret |= parse(path)
    current |= ret
    return ret


def parse(path):
    """
    @brief Parse shells scripts for dependencies.
    @param path: The path of the shell script
    @returns A set of all binaries used in the script.
    """

    global current
    cache = Path(data, "sb", "cache")
    cache.mkdir(parents=True, exist_ok=True)
    sub = path.replace("/", ".")
    if not sub.endswith("."):
        sub += "."
    sub += "cache"

    bin_cache = cache / sub
    ret = set()

    if not bin_cache.is_file() or args.update_cache:
        try:

            # Try and open the file
            with open(path) as binary_file:

                # Get the interpreter.
                shebang = binary_file.readline().strip(" \n")
                if not shebang.startswith("#"):
                    return
                log("Finding dependencies of shell script: ", path)
                # This helps for cases like #!/usr/bin/env bash
                interpreter = shebang[2:].split(" ")
                for shell in interpreter:
                    ret |= add(shell)

                # Don't try and resolve python scripts.
                if "python" in interpreter:
                    return ret

                # Go through each line, ignoring here_docs.
                here_doc = None
                for line in binary_file:
                    stripped = line.strip()

                    # comments
                    if not stripped or stripped.startswith("#"):
                        continue

                    # If we hit a here_doc, skip lines until we reach the end.
                    if "<<" in stripped:
                        s = stripped.split(" ")
                        here_doc = s[s.index("<<") + 1].strip("'\"")
                        continue
                    if here_doc:
                        if stripped == here_doc:
                            here_doc = None
                        continue

                    # Tokenize
                    for s in split(r'\s|(?<=\(|\)|\$|\'|\")', stripped):
                        if len(s) <= 1 \
                                or s == path.split("/")[-1] \
                                or s in builtins \
                                or s.startswith(("-", "\"", "'", "{", "}", ";")) \
                                or any(x in s for x in ["=", "&", "|", "(", ")", "/", "\"", "'", "[", "]", "$", "*"]):
                                    continue
                        ret |= add(s)

        # Don't try and handle compiled binaries.
        except UnicodeDecodeError:
            pass
        if len(ret) > 0:
            with bin_cache.open("w") as file:
                ret_list = list(ret)
                for bin in ret_list[:-1]:
                    file.write(f"{bin} ")
                file.write(ret_list[-1])

    if bin_cache.is_file():
        current |= set(bin_cache.open("r").readline().strip().split(" "))
    return ret
