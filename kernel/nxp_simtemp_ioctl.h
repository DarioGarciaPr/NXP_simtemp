#ifndef _NXP_SIMTEMP_IOCTL_H_
#define _NXP_SIMTEMP_IOCTL_H_

#include <linux/types.h>

struct simtemp_sample {
    __u64 timestamp_ns;   // monotonic timestamp
    __s32 temp_mC;        // milli-degree Celsius
    __u32 flags;          // bit0=NEW_SAMPLE, bit1=THRESHOLD_CROSSED
} __attribute__((packed));

/* Opcional: IOCTL codes */
#define SIMTEMP_IOC_MAGIC  's'
#define SIMTEMP_IOC_RESET_STATS   _IO(SIMTEMP_IOC_MAGIC, 0)
#define SIMTEMP_IOC_GET_STATS     _IOR(SIMTEMP_IOC_MAGIC, 1, struct simtemp_stats)

struct simtemp_stats {
    __u64 updates;
    __u64 alerts;
    __u64 errors;
};

#endif
