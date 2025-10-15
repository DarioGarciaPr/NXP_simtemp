# nxp_simtemp Design Document

## Overview
`nxp_simtemp` is a Linux kernel module that simulates a temperature sensor. It provides a misc device `/dev/nxp_simtemp` for user-space interaction and sysfs attributes for configuration and monitoring.

## Features
- Periodic temperature sampling via a kernel timer (`sample_timer`).
- Modes of operation: `normal`, `noisy`, `ramp`.
- Alert flags when temperature crosses a threshold (`alert_event`).
- Sysfs interface for `threshold`, `sampling`, `mode`, and `stats`.
- File operations: `read()`, `write()`, `poll()`.
- Synchronization using mutexes (`sample_lock`, `stats_lock`, `mode_lock`) and wait queue (`sample_wq`).
- Optional platform device for Device Tree testing.

## Data Structures
### Sample
```c
struct sample {
    int temp_mC;  // Temperature in milli-Celsius
    int alert;     // Alert flag if threshold crossed
};
```

### Sample Record (Binary Output)
```c
struct sample_record {
    u32 timestamp_jiffies;
    int temp_mC;
    u8 alert;
    u8 reserved[3]; // Padding to 12 bytes
};
```

### Statistics
```c
struct nxp_simtemp_stats {
    u64 samples_generated;
    u64 invalid_writes;
    u64 alerts;
};
```

## Sysfs Attributes
- **threshold (RW)**: Temperature threshold in mC.
- **sampling (RW)**: Sampling period in milliseconds.
- **mode (RW)**: Simulation mode: `normal`, `noisy`, `ramp`.
- **stats (RO)**: Module statistics.

## Timer and Sampling
- `sample_timer` triggers periodically according to `sampling_ms`.
- Generates a new sample based on the current `sim_mode`.
- Updates `latest_sample`, sets `sample_ready`, and triggers `alert_event` if threshold is crossed.
- Updates statistics under `stats_lock`.
- Wakes up processes waiting on `sample_wq`.

## File Operations
- `nxp_simtemp_read()`: Blocks until a new sample is available. Returns a binary `sample_record`.
- `nxp_simtemp_write()`: Updates `threshold` if a valid value is provided. Increments `invalid_writes` otherwise.
- `nxp_simtemp_poll()`: Supports `POLLIN` for new samples and `POLLPRI` for alert events.

## Mode Generators
- **normal**: 25°C ±5°C.
- **noisy**: 25°C ±20°C.
- **ramp**: Increases by 1°C per sample up to 100°C, then resets.

## Initialization & Cleanup
- Core init: registers misc device, creates sysfs attributes, sets up timer, initializes first sample.
- Platform device init: optional for DT testing.
- Exit: deletes timer, removes sysfs files, deregisters misc device and platform device.

## Synchronization / Locking Choices
- **sample_lock** (mutex): protects `latest_sample`, `sample_ready`, `alert_event` because access occurs in both timer context and user-space read; blocking mutex is acceptable since timer callback runs in softirq context.
- **stats_lock** (mutex): protects `stats` updates; not performance-critical.
- **mode_lock** (mutex): protects `sim_mode` updates.
- **spinlocks** are not used because all accesses can sleep and contention is minimal.

## Event and Interaction Flow
- Userspace interacts via CLI/GUI.
- CLI/GUI performs `read()`, `write()`, and `poll()` on `/dev/nxp_simtemp`.
- Kernel timer generates new samples and updates `latest_sample`.
- `sample_ready` flag and `alert_event` flag coordinate events.
- `wait_event_interruptible()` in `read()` unblocks when new data is ready.
- `poll()` returns `POLLIN` when sample is ready and `POLLPRI` if threshold is crossed.

## API Trade-offs
- **Sysfs**: used for configuration (`threshold`, `sampling`, `mode`) and stats; easy to expose attributes and monitor from user-space.
- **IOCTL**: not used here because configuration is simple and polling events can be handled by `poll()` and flags; sysfs is simpler and sufficient.

## Device Tree Mapping
- `compatible = "nxp,nxp_simtemp"` in DT triggers `platform_driver` probe.
- Probe registers misc device and initializes sysfs attributes.
- Defaults are used if DT node is missing: misc device still registers with default sampling and threshold.

## Scaling Considerations
- **10 kHz sampling (~0.1 ms per sample)**: current implementation may break due to timer and mutex overhead.
- Wakeups, `copy_to_user`, and mutexes will dominate CPU.
- Strategies to mitigate:
  - Use high-resolution timers (hrtimers) instead of `mod_timer`.
  - Replace mutex with spinlocks if access must be non-blocking in high-frequency context.
  - Batch samples and perform fewer user-space wakeups.
  - Use lockless ring buffer for high-frequency data.

## Flow Diagram
```mermaid
flowchart TD
    A["User Space"] --> B["CLI / GUI"]
    B -->|read/write/poll| C["dev/nxp_simtemp - miscdevice"]
    C --> D["Kernel Module"]
    D --> E["Sysfs: threshold, sampling, mode, stats"]
    D --> F["Timer Callback: sample generation"]
    F --> D
    D -->|update latest_sample, alert_event| G["Wait Queue & Mutex"]
    H["Platform Device / Device Tree"] --> D
```

## References
- Linux kernel module programming guide.
- Linux miscdevice and sysfs documentation.
- Kernel timer and wait queue mechanisms.

---
Design updated to include block diagram, interaction description, locking choices, API trade-offs, Device Tree mapping, and scaling considerations.


