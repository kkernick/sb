# SB++

`sb++` is a C++ implementation of SB. It's faster, easier to use, and more functional than the original Python implementation.

## Building

Run `make` in this directory (`cpp`) to build a native-optimized binary of SB, created at `sb`. You can also choose between more or less optimized recipes, including:

1. `generic`: A generic, x86 binary that can run on all machines. Compiles quickly.
2. `cpp`: A ThinLTO, Native optimized binary.  Compiles moderately fast.
3. `pgo`: A ThinLTO+PGO optimized binary that runs all installed profiles in `~/.local/bin` to optimize the binary specifically for profiles in use. Compiles slowly; has to compile a intermediary profile for profiling.
4. `bolt` A ThinLTO+Optimized+BOLT optimized binary. Very slow
5. `bolt-pgo` A ThinLTO+PGO+BOLT optimized binary. Exceptionally slow, and often times *slower* then regular BOLT.

For reference, here is a example in performance between all four binaries (From `benchmark.sh`, in milliseconds):

| Profile          | `debug` | `generic` | `cpp`     | `pgo`     | `bolt` | `bolt-pgo` |
| ---------------- | ------- | --------- | --------- | --------- | ------ | ---------- |
| Chromium Cold    | 227.0   | 224.3     | **217.1** | 246.2     | 220.0  | 240.0      |
| Chromium Hot     | 4.0     | 3.9       | **3.0**   | 3.1       | 3.1    | 3.5        |
| Chromium Library | 18.1    | 18.5      | 15.3      | **15.2**  | 15.6   | 15.7       |
| Chromium Update  | 258.1   | 258.6     | 222.3     | **222.0** | 224.0  | 224.1      |

Everything after a Generic binary fell within the standard error of each profile, which means that the differences can largely be chalked up to chance. While you probably won't gain *too* much off of just using an optimized, `-03 -march=native` binary in the `cpp` recipe, the `PKGBUILD` defaults to `Optimized+BOLT`. PGO may be faster if you have a more specialized profiling suite.


The following dependencies are needed:

1. `glibc`
2. `findutils`
3. `which`
4. `bubblewrap`
5. `libb2`
6. `libseccomp`

There are also optional runtime dependencies, guarded behind switches, that can add additional functionality:

1. `strace`: To enable the `error` and `strace` `--verbose` values by running the program underneath `strace`.
2. `hardened_malloc`: To enable the `--hardened-malloc` flag, which hardens both the application and proxy sandbox.
3. `zram-generator`: To enable `--fs=zram` to use a compressed ram drive as the backing for profile SOFs, reducing memory usage over `--fs=tmp` and improving performance.
4. `xdg-dbus-proxy`: To enable Flatpak emulation and Portals
supports.
5. `gocryptfs` for `--encrypt`
6. `kdialog` for graphical dialogs
7. `xorg-server-xephyr` for `--xorg`.

## Installation

If you're running an Arch-Based Distribution, you can use the PKGBUILD in this repo. Just run `makepkg -si`. Otherwise, ensure all of the above run-time dependencies are met, and additionally install the following build-time dependencies:

1. `lld` For linking.
2. `clang` For compiling.
3. `llvm` If you want `llvm-bolt` profiling.
4. `doxygen` If you want documentation.

Initialize the third-party submodules via `git submodule init` and `git submodule fetch`. Then, you can either choose to dynamically link against `libexec`, or compile it in with the third-party submodule. If you have `libexec` installed, run `make link`, otherwise `make cpp`. The resulting binary can then be used/installed.

## Scripts and Services

1. `sb.conf` is a zram configuration. If you have `zram-generator` installed and want `sb` to use zram as an SOF backing, copy it to `/usr/lib/systemd/zram-generator.conf.d`
2. `sb.service` is a user systemd service that dry runs applications tagged with `--dry-startup`. To use, copy it to `/usr/lib/systemd/user/`, copy `sb-startup` to the path, and enable the service.
3. `sb-refresh`: Refresh all profile caches. Run after an update.
4. `sb-seccomp`: Append new syscalls to a SECCOMP filter. Use in conjunction with `--seccomp=strace`.
5. `sb-open` is a slimmed down version of `xdg-open`, and is used with the `--xdg-open` switch. Your mileage may vary using the real `xdg-open`.
## Licenses and Third-Party Libraries

* SB++ uses the [BS::ThreadPool](https://github.com/bshoshany/thread-pool) library, which is licensed under the MIT. Without a native thread pool library to compete against Python, SB++ would not be competitive in speed, and such this project would not exist without this library.
## Why?

Speed was the principal reason for implementing SB in C++, and the results are to be expected from moving from Python to C++ (via `benchmark.sh`):

| Profile (ms)                                                         | Cold (P) | **Cold (C)** | Hot (P) | Hot (C)   | Libraries (P) | Libraries (C) | Caches (P) | Caches (C) |
| -------------------------------------------------------------------- | -------- | ------------ | ------- | --------- | ------------- | ------------- | ---------- | ---------- |
| [Chromium](https://github.com/ungoogled-software/ungoogled-chromium) | 1350.0   | **214.6**    | 74.7    | **3.3**   | 310.7         | **8.7**       | 519.2      | **187.1**  |
| [Zed](https://github.com/zed-industries/zed)                         | 996.3    | **303.2**    | 76.4    | **3.6**   | 232.9         | **4.5**       | 267.3      | **68.4**   |
| [Obsidian](https://obsidian.md/)                                     | 1366.6   | **217.3**    | 72.7    | **3.9**   | 290.6         | **5.1**       | 487.2      | **155.2**  |
| [Fooyin](https://github.com/fooyin/fooyin)                           | 5439.4   | **658.8**    | 72.7    | **3.1**   | 1494.6        | **6.7**       | 2588.3     | **1036.2** |
| [Okular](https://invent.kde.org/graphics/okular)                     | 5152.2   | **634.0**    | 73.3    | **2.9**   | 1303.6        | **9.1**       | 2387.5     | **993.0**  |
| [KeePassXC](https://github.com/keepassxreboot/keepassxc)             | 5069.6   | **640.7**    | 73.0    | **2.9**   | 1211.2        | **9.1**       | 2224.1     | **999.2**  |
| [Syncthing](https://github.com/syncthing/syncthing)                  | 161.7    | **8.3**      | 72.0    | **3.6**   | 111.5         | **4.0**       | 116.5      | **19.0**   |
| [Yarr](https://github.com/nkanaev/yarr)                              | 163.0    | **8.7**      | 72.1    | **3.6**   | 116.3         | **4.0**       | 121.6      | **19.0**   |
| Average                                                              | 2462.3   | **335.7**    | 83.8    | **3.4**   | 633.9         | **6.4**       | 1089.0     | **309.7**  |
| Speedup                                                              |          | **733%**     |         | **2465%** |               | **9904%**     |            | **352%**   |

* Cold Launch is an important metric if the startup service isn't be used, as it determines how long a program will take to launch for the first time after booting.  Applications can benefit from the loading of other applications (One Qt application will populate the shared SOF for other Qt applications, letting it launch "warm").
* Hot Launch is the most important metric for SB. It defines how fast the program can launch with a warm SOF and both a `lib.cache` and `cmd.cache`. It effectively measures how quickly SB can read the `cmd.cache` and launch `bwrap`. The ideal is for this value to be zero, which would be equivalent to launching the application directly.
* Update Libraries/Caches are important metrics for when the caches needs to be updated, particularly after an update.