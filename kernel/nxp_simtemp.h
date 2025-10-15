#ifndef NXP_SIMTEMP_H
#define NXP_SIMTEMP_H

#include <linux/types.h>

#define FLAG_NEW_SAMPLE        0x1
#define FLAG_THRESHOLD_CROSSED 0x2

// Registro de temperatura
struct simtemp_sample {
    __u64 timestamp_ns;   // monotonic timestamp
    __s32 temp_mC;        // milli-degree Celsius
    __u32 flags;          // bit0=NEW_SAMPLE, bit1=THRESHOLD_CROSSED
} __attribute__((packed));

#endif // NXP_SIMTEMP_H

