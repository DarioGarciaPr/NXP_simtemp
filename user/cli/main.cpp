#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>

#define DEVICE "/dev/nxp_simtemp"
#define SYSFS_THRESHOLD "/sys/class/misc/nxp_simtemp/threshold"
#define SYSFS_SAMPLING "/sys/class/misc/nxp_simtemp/sampling"

struct sample_record {
    uint32_t timestamp_jiffies;
    int temp_mC;
    uint8_t alert;
    uint8_t reserved[3];
};

// Read threshold from sysfs
int read_threshold() {
    int fd = open(SYSFS_THRESHOLD, O_RDONLY);
    if (fd < 0) {
        perror("Error reading threshold");
        return -1;
    }
    char buf[32];
    ssize_t n = read(fd, buf, sizeof(buf)-1);
    close(fd);
    if (n <= 0) return -1;
    buf[n] = '\0';
    return atoi(buf);
}

// Read sampling from sysfs
int read_sampling() {
    int fd = open(SYSFS_SAMPLING, O_RDONLY);
    if (fd < 0) {
        perror("Error reading sampling_ms");
        return -1;
    }
    char buf[32];
    ssize_t n = read(fd, buf, sizeof(buf)-1);
    close(fd);
    if (n <= 0) return -1;
    buf[n] = '\0';
    return atoi(buf);
}

// Write threshold to sysfs
int write_threshold(int value) {
    int fd = open(SYSFS_THRESHOLD, O_WRONLY);
    if (fd < 0) {
        perror("Error writing threshold");
        return -1;
    }
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "%d", value);
    if (write(fd, buf, len) != len) {
        perror("Error writing threshold");
        close(fd);
        return -1;
    }
    close(fd);
    return 0;
}

// Write sampling time to sysfs
int write_sampling(int value) {
    int fd = open(SYSFS_SAMPLING, O_WRONLY | O_SYNC);
    if (fd < 0) {
        perror("Error opening sampling_ms");
        return -1;
    }
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "%d", value);
    if (write(fd, buf, len) != len) {
        perror("Error writing sampling_ms");
        close(fd);
        return -1;
    }
    
    fsync(fd);
    close(fd);
    return 0;
}

int main(int argc, char *argv[]) {
    int max_reads = -1; // infinite by default

    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "set") == 0 && i + 2 < argc && strcmp(argv[i+1], "threshold") == 0) {
        int t = atoi(argv[i+2]);
        if (write_threshold(t) == 0)
          printf("Threshold updated to %d\n", t);
        return 0;
        }
        else if (strcmp(argv[i], "set") == 0 && i + 2 < argc && strcmp(argv[i+1], "sampling") == 0) {
        int s = atoi(argv[i+2]);
        if (write_sampling(s) == 0)
            printf("Sampling time updated to %d ms\n", s);
        return 0;
      }
    // --- get threshold ---
    else if (strcmp(argv[i], "get") == 0 && i + 1 < argc && strcmp(argv[i+1], "threshold") == 0) {
        int t = read_threshold();
        if (t >= 0)
            printf("Current threshold: %d\n", t);
        return 0;
      }
    // --- get sampling ---
    else if (strcmp(argv[i], "get") == 0 && i + 1 < argc && strcmp(argv[i+1], "sampling") == 0) {
        int s = read_sampling();
        if (s >= 0)
            printf("Current sampling time: %d ms\n", s);
        return 0;
      }
      else if (strcmp(argv[i], "--count") == 0 && i + 1 < argc) {
            max_reads = atoi(argv[i+1]);
            i++;
        }
    }

    int fd = open(DEVICE, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        perror("Error opening device");
        return 1;
    }

    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN | POLLPRI;

    int reads = 0;

    printf("Reading from %s...\n", DEVICE);
    int th = read_threshold();
    if (th >= 0)
        printf("Current threshold: %d\n", th);

    while (max_reads < 0 || reads < max_reads) {
        int ret = poll(&pfd, 1, 5000); // 5s timeout
        if (ret < 0) {
            perror("poll");
            break;
        } else if (ret == 0) {
            continue; // timeout
        }

        if (pfd.revents & POLLIN) {
            struct sample_record rec;
            ssize_t n = read(fd, &rec, sizeof(rec));
            if (n == sizeof(rec)) {
                struct timespec ts;
                clock_gettime(CLOCK_REALTIME, &ts);
                struct tm tm_info;
                gmtime_r(&ts.tv_sec, &tm_info);
                char time_buf[64];
                strftime(time_buf, sizeof(time_buf), "%Y-%m-%dT%H:%M:%S", &tm_info);

                printf("%s.%03ldZ temp=%.3fC alert=%d%s\n",
                       time_buf,
                       ts.tv_nsec / 1000000,
                       rec.temp_mC / 1000.0,
                       rec.alert,
                       rec.alert ? " <-- THRESHOLD CROSSING!" : "");
                reads++;
            }
        }

        if (pfd.revents & POLLPRI) {
            // optional: handle special threshold crossing event
            printf("** Threshold crossing event (POLLPRI) detected **\n");
        }
    }

    close(fd);
    return 0;
}

