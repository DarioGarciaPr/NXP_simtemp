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

## T1 – Load/Unload Module

Objective: Verify module loads, creates /dev/nxp_simtemp and sysfs files, and unloads cleanly.

Steps:

1. Ensure module is not loaded: lsmod | grep nxp_simtemp
2. sudo insmod nxp_simtemp.ko
3. Check /dev/nxp_simtemp exists
4. Check sysfs attributes exist
5. sudo rmmod nxp_simtemp
6. Verify cleanup: device removed, no dangling timers

Expected Result: Module loads/unloads without errors, device and sysfs exist during load.

## T2 – Sysfs Configuration

Objective: Set threshold and sampling via sysfs.

Steps:

1. Load module
2. echo 45000 | sudo tee /sys/class/misc/nxp_simtemp/threshold
3. echo 100 | sudo tee /sys/class/misc/nxp_simtemp/sampling
4. Read back values to verify
5. Execute CLI to read 10 samples: ./main --count 10

Expected Result: Threshold and sampling are correctly applied. CLI reads samples without errors.

## T3 – Threshold Write via CLI

Objective: Verify CLI can modify threshold.

Steps:

1. Load module
2. ./main set 35000
3. Read samples with CLI to check alert behavior
4. Verify threshold via sysfs

Expected Result: Threshold changes correctly and affects alert generation.

## T4 – Error Paths

Objective: Ensure module handles invalid sysfs input gracefully.

Steps:

1. Load module
2. Attempt: echo "invalid" | sudo tee /sys/class/misc/nxp_simtemp/threshold
3. Observe kernel logs / CLI behavior
Expected Result: Module rejects invalid input (-EINVAL), no crash occurs.

## T5 – Concurrency Demo

Objective: Test safe access while module is being read and threshold updated.

Steps:

1. Load module
2. Start CLI reading samples in background
3. While CLI is running, change threshold via sysfs
4. Wait for CLI to complete

Expected Result: No race conditions, CLI reads correct samples, threshold change applies safely.

## T6 – API Contract / Partial Reads

Objective: Verify read() and struct handling.

Steps:

1. Load module
2. Use CLI to perform multiple reads
3. Check returned format and endianness
4. Validate partial/short reads are handled correctly

Expected Result: Read data conforms to expected format; module handles partial reads.

## T7 – Poll Notification (Optional)

Objective: Verify non-blocking poll() interface.

Steps:

1. Load module
2. Use CLI/GUI or select() in a test script to poll /dev/nxp_simtemp
3. Wait for new sample
4. Verify poll returns when sample ready

Expected Result: Poll returns immediately when sample is ready; blocking behavior works correctly.

## T8 – GUI Integration (Optional)

Objective: Test simtemp_gui.py functionality.

Steps:

1. Load module
2. Start GUI: python3 simtemp_gui.py
3. Verify display of current temperature and alert
4. Modify threshold via GUI
5. Observe correct behavior on temperature samples

Expected Result: GUI shows updated values and interacts with sysfs and misc device correctly.

## T9 – Device Tree / Platform Device (Optional)

Objective: Verify DT-based platform device integration.

Steps:

1. Build module with platform device support
2. Check kernel logs for platform device created for DT test
3. Ensure module works independently of DT node

Expected Result: Platform device registers without impacting core functionality.

## Notes

* All tests must pass without kernel panics or warnings.
* Use dmesg to monitor kernel logs during testing.
* Optional tests (GUI, DT, poll) provide full integration coverage.
