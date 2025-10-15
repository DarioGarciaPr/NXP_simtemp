# Test Plan for nxp_simtemp

## Overview

This document describes the testing strategy for the nxp_simtemp Linux kernel module.
It ensures correct operation of:
* Periodic temperature sampling
* Threshold alert generation
* CLI/GUI interaction
* Sysfs read/write interfaces
* Poll-based non-blocking notification
* Optional Device Tree / Platform Device integration

## Test Environment

* Linux x86_64, kernel 6.14.0-33
* GCC 13
* Python 3.12 for GUI
* nxp_simtemp.ko kernel module
* CLI compiled as main
* Sysfs paths: /sys/class/misc/nxp_simtemp/threshold, /sys/class/misc/nxp_simtemp/sampling
* Optional: QEMU / i.MX for DT testing

# Test Scenarios
## Tests

| Test | Input / Setup | Action | Expected Output | Sysfs / Commands |
|------|---------------|--------|-----------------|-----------------|
| **T1 – Build & Load/Unload** | Module and CLI sources | Compile module: `make -C kernel clean && make -C kernel`; compile CLI: `g++ -O2 -Wall -std=c++17 -o user/cli/main user/cli/main.cpp`; load/unload module | Module loads/unloads without errors; `/dev/nxp_simtemp` and sysfs files exist | `sudo insmod kernel/nxp_simtemp.ko`; check `/dev/nxp_simtemp`; `ls /sys/class/misc/nxp_simtemp` |
| **T2 – Periodic Sampling** | `sampling_ms = 100` | Read 10 samples via CLI | Samples returned ~100ms apart; `stats.samples_generated` increments | `echo 100 | sudo tee /sys/class/misc/nxp_simtemp/sampling`; `user/cli/main --count 10`; `cat /sys/class/misc/nxp_simtemp/stats` |
| **T3 – Threshold Event / Poll** | Threshold below expected mean (e.g., 34500 mC) | CLI reads samples in background | CLI unblocks when threshold crossed; `alert_event` set | `echo 34500 | sudo tee /sys/class/misc/nxp_simtemp/threshold`; `user/cli/main --count 5 &` |
| **T4 – Error Paths** | Invalid threshold; very fast sampling | Write invalid threshold; write `1ms` sampling; read stats | Invalid writes return `-EINVAL` and increment `stats.invalid_writes`; module remains responsive | `echo invalid | sudo tee /sys/class/misc/nxp_simtemp/threshold`; `echo 1 | sudo tee /sys/class/misc/nxp_simtemp/sampling`; `cat /sys/class/misc/nxp_simtemp/stats` |
| **T5 – Concurrency** | CLI reading periodic samples | Change threshold during CLI read | CLI continues without blocking; threshold updates correctly; no corruption | `user/cli/main --count 10 &`; `echo 30000 | sudo tee /sys/class/misc/nxp_simtemp/threshold` |
| **T6 – API Contract / Struct Validation** | Binary struct read | Read multiple samples via CLI | `sample_record` fields correct: `timestamp_jiffies`, `temp_mC`, `alert`, padding; correct endianness | `user/cli/main --count 5` |
| **T7 – Mode Attribute** | Modes: normal, noisy, ramp | Set mode via sysfs; read 5 samples per mode | `sim_mode` updated correctly; temperature generation matches mode behavior | `echo normal|noisy|ramp | sudo tee /sys/class/misc/nxp_simtemp/mode`; `user/cli/main --count 5` |
| **Quick Check / Smoke Test** | Module loaded | Read 3 samples quickly | Samples returned; no kernel errors | `user/cli/main --count 3` |

---

### Notes
- Mutexes (`sample_lock`, `stats_lock`, `mode_lock`) and wait queue (`sample_wq`) ensure data integrity.
- `read()` blocks until `sample_ready = true`.
- `alert_event` triggers only when temperature crosses threshold.
- Sysfs provides configuration (`threshold`, `sampling`, `mode`) and monitoring (`stats`).


