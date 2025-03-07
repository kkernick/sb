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

| Profile         | Generic | Optimized | PGO   | PGO+BOLT | Optimized+BOLT |
| --------------- | ------- | --------- | ----- | -------- | -------------- |
| Chromium Cold   | 319.5   | 313.3     | 311.4 | 311.6    | 310.8          |
| Chromium Hot    | 37.2    | 35.1      | 35.4  | 35.2     | 35.3           |
| Chromium Update | 233.9   | 225.1     | 225.6 | 225.7    | 224.1          |
Everything after a Generic binary fell within the standard error of each profile, which means that the differences can largely be chalked up to chance. While you probably won't gain *too* much off of just using an optimized, `-03 -march=native` binary in the `cpp` recipe, the `PKGBUILD` defaults to `Optimized+BOLT`. PGO may be faster if you have a more specialized profiling suite.


The following dependencies are needed:

1. `glibc`
2. `findutils`
3. `which`
4. `bubblewrap`

There are also optional runtime dependencies, guarded behind switches, that can add additional functionality:

1. `strace`: To enable the `error` and `strace` `--verbose` values by running the program underneath `strace`.
2. `hardened_malloc`: To enable the `--hardened-malloc` flag, which hardens both the application and proxy sandbox.
3. `zram-generator`: To enable `--fs=zram` to use a compressed ram drive as the backing for profile SOFs, reducing memory usage over `--fs=tmp` and improving performance.
4. `xdg-dbus-proxy`: To enable Flatpak emulation and Portals
supports.
5. `libseccomp`: To enable `--seccomp` to further harden the sandbox by permitting what syscalls are allowed to be run.

## Scripts and Services

The following files are located at the root of the repo, and are shared between both implementations:

1. `sb.conf` is a zram configuration. If you have `zram-generator` installed and want `sb` to use zram as an SOF backing, copy it to `/usr/lib/systemd/zram-generator.conf.d`
2. `sb.service` is a user systemd service that dry runs applications tagged with `--dry-startup`. To use, copy it to `/usr/lib/systemd/user/`, copy `sb-startup` to the path, and enable the service.
3. `sb-open` is a slimmed down version of `xdg-open`, and is used with the `--xdg-open` switch. Your mileage may vary using the real `xdg-open`.

The following files are specific to SB++

1. `sb-refresh`: Refresh all profile caches. Run after an update.
2. `sb-seccomp`: Append new syscalls to a SECCOMP filter. Use in conjunction with `--seccomp=strace`.
3. `sb-startup`: The startup worker.

## Licenses and Third-Party Libraries

* SB++ uses the [BS::ThreadPool](https://github.com/bshoshany/thread-pool) library, which is licensed under the MIT. Without a native thread pool library to compete against Python, SB++ would not be competitive in speed, and such this project would not exist without this library.
## Why?

### Speed

Speed was the principal reason for implementing SB in C++, and the results are to be expected from moving from Python to C++ (via `benchmark.sh`):

| Profile (C/P)                                                        | Cold Launch     | Hot Launch      | Update Libraries | Update Caches   |
| -------------------------------------------------------------------- | --------------- | --------------- | ---------------- | --------------- |
| [Chromium](https://github.com/ungoogled-software/ungoogled-chromium) | 274.2/1173.8    | 10.4/66.9       | 23.0/247.3       | 195.0/414.3     |
| [Zed](https://github.com/zed-industries/zed)                         | 347.8/892.3     | 6.6/64.2        | 16.1/194.3       | 58.9/230.5      |
| [Obsidian](https://obsidian.md/)                                     | 255.6/1090.5    | 6.4/62.7        | 16.4/234.4       | 239.1/372.7     |
| [Fooyin](https://github.com/fooyin/fooyin)                           | 374.6/3456.0    | 7.3/63.4        | 12.0/1098.1      | 89.0/1874.0     |
| [Okular](https://invent.kde.org/graphics/okular)                     | 379.5/3125.3    | 7.3/63.7        | 15.1/923.1       | 86.7/1719.4     |
| [KeePassXC](https://github.com/keepassxreboot/keepassxc)             | 424.3/3081.2    | 4.6/62.8        | 13.7/828.4       | 85.8/1625.3     |
| [Syncthing](https://github.com/syncthing/syncthing)                  | 53.4/133.2      | 5.6/60.8        | 14.8/93.1        | 21.7/97.3       |
| [Yarr](https://github.com/nkanaev/yarr)                              | 29.8/135.0      | 5.3/61.0        | 10.2/98.1        | 17.4/101.6      |
| Average Speedup                                                      | **612% Faster** | **945% Faster** | **3061% Faster** | **811% Faster** |
* Cold Launch is an important metric if the startup service isn't be used, as it determines how long a program will take to launch for the first time after booting.  Applications can benefit from the loading of other applications (One Qt application will populate the shared SOF for other Qt applications, letting it launch "warm").
* Hot Launch is the most important metric for SB. It defines how fast the program can launch with a warm SOF and both a `lib.cache` and `cmd.cache`. It effectively measures how quickly SB can read the `cmd.cache` and launch `bwrap`. The ideal is for this value to be zero, which would be equivalent to launching the application directly.
* Update Libraries/Caches are important metrics for when the caches needs to be updated, particularly after an update. 

Besides the improvements inherent in the change of language, SB++ implements many improvements to the underlying algorithms, yielding further performance gains:
* Cold Launch has been massively improved through two key improvements:
	1. Rather than reading files as binary and writing them to the SOF, SB++ uses `std::filesystem::copy_file` to use internal copying mechanisms that are far faster.
	2. Copying to the SOF is threaded; rather than copying files sequentially they are processed by a pool.
* Hot Launch has been improved by using `fork/dup2/exec` directly, foregoing the need to launch a shell. 
* Library Updates fix a bug in the Python implementation, where `ldd` was assumed to not recursively determine dependencies. It does. This speed up allows for a far more reliable parser, mentioned below.
* Cache Updates no longer using a thread-mediated `searched` list, to which mutex access significantly slowed access down. Instead, we just cache `ldd` results, and return the pre-computed results immediately. You may think that returning libraries instead of just an empty list would slow things down, but the performance gain is tremendous, and come with the benefit in that it is more accurate, as the prior implementation had a bug where caches would not be complete if one of their libraries/binaries were already check prior in the sandbox construction.

### Functionality and Usability

New functionality and usability improvements were also a major consideration, with many improvements infeasible or impossible within the confines of Python. Such improvements include:
* A custom, flexible argument handler:
	* Rather than `--electron --electron-version 33`, you can combine them into `--electron 33`. The old argument handler could not support a flag that is either on/off and one that can be given a value.
	* Rather than `--fs=cache --fs-location=md`, you can again combine them using a modifier `--fs=cache:md`. 
	* Arguments are consumed first-come, first-serve, and unless an argument has been explicitly defined as only being set once, you can replace previously defined values. This allows for:
		* Overriding configuration. If you have a `.sb` file that defines `--fs=cache`, and want to temporarily make it persist, you can simply call `profile.sb --fs=persist` and the earlier definition will be ignored. This does not affect list behavior, and you can chain them like `--binaries cat --binaries ls`
		* Inherited profiles. If a `.sb` profile calls another `.sb` program, SB++ will source the called script for arguments and then apply the newly defined changes atop it. Specifically, you can create *clean* profiles, such as by defining a script that calls `sb app.sb --fs=none`, particularly useful for applications like Chromium.
		* Combined Flag and Keypair semantics. The Python argument handler had `store_true` and `store_false` values, which allowed for values like `--hardened-malloc`. However, these semantics made it impossible to retroactively *disable* such flags. If a configuration was defined in `$XDG_CONFIG_HOME/sb.conf`, it could *not* be disabled since the flag was a `store_true`. Now, *all* values can either be explicitly set or toggled. This allows for:
			* Using `--hardened-malloc` like a flag, or explicitly setting it via `--hardened-malloc=true` or `--hardened-malloc=false`.
			* Defining named levels for incrementing switches. `--verbose` can be used as a counter flag, such as `-vvv`, but you can also define the level (Which is outlined in `--help`), such as `--verbose=error`. 
		* Modifier values can be used to extend values by using a `:` as a delimiter. This allows for:
			* *Excluding* libraries by using `:x`. Our new library parser is now *too* good, and draws in `libQt6Core*` from Chromium's shims, which then complains because the corresponding Wayland libraries do not exist. We can use `--libraries libQt6:x` to exclude them from the sandbox.
			* Specify explicit permissions for `--files`, such as `--files file.txt:ro file.csv:rw`.
	* Values can be reset to the default using `!`. This is particularly useful for:
		* Resetting a list. If we want to create a clean profile using inheritance semantics that does not have access to portals, we need to reset the `--portals` flag. Our profile is as easy as `sb profile.sb --profiles !` to drop all values in `--portals`. 
		* Defaulting a flag. While you can always explicitly provide a value, it can be useful to default earlier flags without caring about the underlying default value. Such resets will ignore `sb.conf` overrides. 
* Combined functionality:
	* `--sockets` was a useless flag; `--sockets wayland` would never not be combined with `--dri` or a super-set, to which `--dri` quickly outgrew its original name. Now, both flags have been combined into `--gui`, which still exists as a base for `--qt` and `--gtk`.
	* `--qt --qt5 --kde` were similar in function, and it wasn't entirely clear when to use one over another. The distinction between vanilla Qt applications and those using KDE's Frameworks is a good distinction since the former wouldn't need the latter libraries, but these could be more easily combined thanks to our new argument parser via `--qt 5/6/kf6`.
	* `--strace` has been combined into `--verbose`, since it's a means of logging. There's no `--verbose=error`, which will use `strace` to only report errors, whereas `--verbose=strace` will dump everything.
	* `--seccomp-log` has been removed in favor of an SELinux style flag via `--seccomp permissive/enforcing/strace`. The latter option captures `strace` output to parse syscalls, which will not exhaustive and still requires `sb-seccomp`, provides a base and doesn't require root.
	* `--update-libraries` and `--update-cache` have been combined into `--update false/libraries/cache/all`, and are much faster thanks to faster algorithms.
	* `--post` now uses a modifier so you can specify command and argument in one flag, such as `--post chromium.desktop.sb:"https://localhost:7070"`
* Faster algorithms:
	* The `exec` handler now doesn't invoke a shell, and uses `fork/dup2/execv` rather than `popen` to hasten execution. 
	* Writing the SOF is threaded to tremendous boost cold start speed.
	* The Library parser relies on `ldd`'s recursive functionality, the speed up allowing it to be far more exhaustive and complete than the Python implementation. *So exhaustive* that profiles like Chromium needed to *exclude* libraries from the search (Not because they were erroneous, but because the shim does indeed pull Qt). The result of this is less manual calls to `--libraries` and less time using `strace`
	* The Binary parser is far more advanced. It parser variables to resolve them latter in the script, and use the shell itself to interpret lines. This allows for electron scripts to resolve the `/usr/lib/${electron_version}/electron` calls and actually pulls in the library.
* New Functionality
	* The Proxy can now use hardened malloc.
	* By using `inotify` and `libseccomp` directly, we remove all runtime libraries besides those that add more functionality.