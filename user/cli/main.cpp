#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <time.h>
#include <string.h>
#include <errno.h>

#define DEVICE "/dev/nxp_simtemp"
#define SYSFS_THRESHOLD "/sys/class/misc/nxp_simtemp/threshold"
#define BUF_SIZE 128

// Leer threshold desde sysfs
int read_threshold() {
    int fd = open(SYSFS_THRESHOLD, O_RDONLY);
    if (fd < 0) {
        perror("Error leyendo threshold");
        return -1;
    }
    char buf[32];
    ssize_t n = read(fd, buf, sizeof(buf)-1);
    close(fd);
    if (n <= 0) return -1;
    buf[n] = '\0';
    return atoi(buf);
}

// Escribir threshold a sysfs
int write_threshold(int value) {
    int fd = open(SYSFS_THRESHOLD, O_WRONLY);
    if (fd < 0) {
        perror("Error escribiendo threshold");
        return -1;
    }
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "%d", value);
    if (write(fd, buf, len) != len) {
        perror("Error escribiendo threshold");
        close(fd);
        return -1;
    }
    close(fd);
    return 0;
}

int main(int argc, char *argv[]) {
    int max_reads = -1; // infinito por defecto

    // Parsing argumentos
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "set") == 0 && i + 1 < argc) {
            int t = atoi(argv[i+1]);
            if (write_threshold(t) == 0)
                printf("Threshold actualizado a %d\n", t);
            return 0;
        } else if (strcmp(argv[i], "get") == 0) {
            int t = read_threshold();
            if (t >= 0)
                printf("Threshold actual: %d\n", t);
            return 0;
        } else if (strcmp(argv[i], "--count") == 0 && i + 1 < argc) {
            max_reads = atoi(argv[i+1]);
            i++;
        }
    }

    int fd = open(DEVICE, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        perror("Error abriendo el device");
        return 1;
    }

    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;

    char buf[BUF_SIZE];
    int reads = 0;

    printf("Leyendo de %s...\n", DEVICE);
    int th = read_threshold();
    if (th >= 0)
        printf("Threshold actual: %d\n", th);

    while (max_reads < 0 || reads < max_reads) {
        int ret = poll(&pfd, 1, 5000); // timeout 5s
        if (ret < 0) {
            perror("poll");
            break;
        } else if (ret == 0) {
            continue; // timeout
        }

        if (pfd.revents & POLLIN) {
            ssize_t n = read(fd, buf, sizeof(buf)-1);
            if (n > 0) {
                buf[n] = '\0';
                double temp;
                int alert;
                if (sscanf(buf, "%*u temp=%lfC alert=%d", &temp, &alert) == 2) {
                    struct timespec ts;
                    clock_gettime(CLOCK_REALTIME, &ts);
                    struct tm tm_info;
                    gmtime_r(&ts.tv_sec, &tm_info);

                    char time_buf[64];
                    strftime(time_buf, sizeof(time_buf), "%Y-%m-%dT%H:%M:%S", &tm_info);

                    printf("%s.%03ldZ temp=%.1fC alert=%d\n",
                           time_buf,
                           ts.tv_nsec / 1000000,
                           temp,
                           alert
                    );
                    reads++;
                } else {
                    printf("%s", buf);
                }
            }
        }
    }

    close(fd);
    return 0;
}

