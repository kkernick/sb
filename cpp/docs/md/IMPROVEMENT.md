# Improvement

This file tracks performance between various versions of SB++, based on commit. It should be noted that decreases in performance comes with new features, a simplification of the codebase, or other changes, and as such it not necessarily desirable to have a consistently downward slope; that said, periodic testing can illustrate regressions.

If you use Obsidian, you can visualize these tables directly via *Obsidian Charts*. All tests were done on the Chromium profile available in `examples`, and profiled via the `benchmark.sh` script using the `cpp` recipe or its equivalent at the time. To repeat any of these benchmarks, simply run `./benchmark.sh cpp COMMIT chromium.desktop.sb cpp`  for the desired commit, not that the Python benchmarks will not work with this as the folder structure does not support it, and will instead require manual checkout.

The following table shows the speed of Python-SB at its last update before SB++ was introduced. The values skew the charts too much for them the be useful, so they're included here for reference and comparison

| Commit  | Cold   | Hot  | Libraries | Cache |
| ------- | ------ | ---- | --------- | ----- |
| b4eaac9 | 2204.0 | 66.0 | 476.0     | 648.0 |

| Count | Commit  | Cold  | Hot  | Libraries | Cache | Note                         |
| ----- | ------- | ----- | ---- | --------- | ----- | ---------------------------- |
| 1     | 4e5dec2 | 399.6 | 14.8 | 32.1      | 514.1 | Initial C++ implementation   |
| 2     | 65a7a51 | 381.4 | 12.9 | 29.1      | 508.1 | SECCOMP support              |
| 3     | 9981ea8 | 345.4 | 14.0 | 29.7      | 521.6 | Library and Subprocess Fixes |
| 4     | a05f6c9 | 335.2 | 7.2  | 23.1      | 518.7 | Fix Zombie Processes         |
| 5     | 79d444e | 352.8 | 7.4  | 24.6      | 523.9 | Library Exclusions           |
| 6     | abd1458 | 360.5 | 7.5  | 24.1      | 525.1 | Use of `libb2`               |
| 7     | a524e4f | 362.7 | 7.2  | 23.5      | 466.5 | More prolific thread use.    |
| 8     | daeefd4 | 355.7 | 6.9  | 22.6      | 450.0 | Fix Zombie Processes Again   |
| 9     | 0e7c1ae | ERR   | 6.8  | 22.5      | 447.5 | Proxy Startup Improvements   |
| 10    | 7d34e08 | 351.8 | 7.1  | 23.0      | 450.7 | SOF Optimization             |
| 11    | 4b0da0d | 351.3 | 6.6  | 22.3      | 457.3 | `reserve` and `string_view`  |
| 12    | 199eeab | 347.0 | 3.7  | 19.3      | 448.6 | Performance Fixes            |
| 13    | 41c6b67 | 333.6 | 3.9  | 20.1      | 450.2 | Initializer Lists            |
| 14    | a777123 | 338.3 | 3.9  | 20.1      | 456.9 | Proxy Library Cache          |
| 15    | 1cc53e6 | 345.3 | 3.8  | 24.3      | 397.3 | Don't use `which`            |
| 16    | c6e7341 | 346.1 | 3.7  | 22.0      | 389.1 | "Optimization"               |
| 17    | 29b01b6 | 275.3 | 5.1  | 18.2      | 269.9 | "Documentation"              |
| 18    | 22074db | 275.6 | 4.0  | 18.4      | 265.1 | LDD Corrections.             |
| 19    | 0a01c31 | 212.7 | 3.8  | 10.2      | 200.8 | Cleanup switches             |
^times

```chart
type: line
id: times
select: [Cold]
tension: 0.5
spanGaps: true
```

```chart
type: line
id: times
select: [Hot]
tension: 0.5
```

```chart
type: line
id: times
select: [Libraries]
tension: 0.5
```

```chart
type: line
id: times
select: [Cache]
tension: 0.5
```
