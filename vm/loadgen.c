#define _GNU_SOURCE

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define CYCLE_NS 20000000LL
#define MEM_SIZE (64 * 1024 * 1024)
#define LIBCRYPTO_PATH "/home/seed/opt/openssl-1.1.0f/lib/libcrypto.so"

static const size_t PROBE_OFFSETS[] = {
    0x1db000, 0x1db400, 0x1db800, 0x1dbc00,
};

static uint64_t now_ns(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

static void do_busy(void)
{
    volatile uint64_t x = 1;

    for (int i = 0; i < 2000; i++)
        x = x * 3 + (uint64_t)i;
}

static void do_mem(uint8_t *buf)
{
    static uint64_t state = 0x123456789abcdefULL;

    for (int i = 0; i < 256; i++) {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;

        size_t line = state % (MEM_SIZE / 64);
        buf[line * 64]++;
    }
}

static void maccess(const void *addr)
{
    asm volatile(
        "movq (%0), %%rax\n"
        :
        : "c"(addr)
        : "rax", "memory"
    );
}

static void flush(const void *addr)
{
    asm volatile(
        "clflush 0(%0)\n"
        :
        : "c"(addr)
        : "memory"
    );
}

static void do_target(const uint8_t *mapping)
{
    for (size_t i = 0; i < sizeof(PROBE_OFFSETS) / sizeof(PROBE_OFFSETS[0]); i++)
        maccess(mapping + PROBE_OFFSETS[i]);
}

static void do_evict(const uint8_t *mapping)
{
    for (size_t i = 0; i < sizeof(PROBE_OFFSETS) / sizeof(PROBE_OFFSETS[0]); i++)
        flush(mapping + PROBE_OFFSETS[i]);
}

static uint8_t *map_libcrypto(void)
{
    int fd = open(LIBCRYPTO_PATH, O_RDONLY);
    if (fd < 0) {
        perror("open libcrypto");
        return NULL;
    }

    struct stat st;
    if (fstat(fd, &st) != 0) {
        perror("fstat libcrypto");
        close(fd);
        return NULL;
    }
    if ((size_t)st.st_size <= PROBE_OFFSETS[3]) {
        fprintf(stderr, "libcrypto is too small for the probe offsets\n");
        close(fd);
        return NULL;
    }

    uint8_t *mapping = mmap(NULL, st.st_size, PROT_READ, MAP_SHARED, fd, 0);
    close(fd);
    if (mapping == MAP_FAILED) {
        perror("mmap libcrypto");
        return NULL;
    }
    return mapping;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "Usage: %s {busy|mem|target|evict} DUTY_PCT\n", argv[0]);
        return 1;
    }

    int is_busy = !strcmp(argv[1], "busy");
    int is_mem = !strcmp(argv[1], "mem");
    int is_target = !strcmp(argv[1], "target");
    int is_evict = !strcmp(argv[1], "evict");
    if (!is_busy && !is_mem && !is_target && !is_evict) {
        fprintf(stderr, "Mode must be busy, mem, target, or evict\n");
        return 1;
    }

    int duty = atoi(argv[2]);
    if (duty < 0)
        duty = 0;
    if (duty > 100)
        duty = 100;

    uint8_t *buf = NULL;
    if (is_mem) {
        buf = malloc(MEM_SIZE);
        if (!buf) {
            perror("malloc");
            return 1;
        }
        memset(buf, 0xaa, MEM_SIZE);
    }

    uint8_t *mapping = NULL;
    if (is_target || is_evict) {
        mapping = map_libcrypto();
        if (!mapping)
            return 1;
    }

    uint64_t on_ns = CYCLE_NS * (uint64_t)duty / 100;
    uint64_t off_ns = CYCLE_NS - on_ns;

    for (;;) {
        uint64_t t0 = now_ns();
        while (now_ns() - t0 < on_ns) {
            if (is_mem)
                do_mem(buf);
            else if (is_target)
                do_target(mapping);
            else if (is_evict)
                do_evict(mapping);
            else
                do_busy();
        }
        if (off_ns) {
            struct timespec ts = {
                off_ns / 1000000000ULL,
                off_ns % 1000000000ULL,
            };
            nanosleep(&ts, NULL);
        }
    }
}
