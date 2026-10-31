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

#define CYCLE_NS   20000000LL        /* 20 ms duty-cycle window */
#define MEM_SIZE   (256ULL * 1024 * 1024)
#define SLOT_SIZE  64
#define LIBCRYPTO_PATH "/home/seed/opt/openssl-1.1.0f/lib/libcrypto.so"

static const size_t PROBE_OFFSETS[] = {
    0x1db000, 0x1db400, 0x1db800, 0x1dbc00,
};
#define N_PROBES (sizeof(PROBE_OFFSETS) / sizeof(PROBE_OFFSETS[0]))

static uint8_t *mem_buf;
static size_t stream_idx;
static uint8_t *crypto_map;

static uint64_t now_ns(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

static void do_busy(void) {
  volatile uint64_t x = 1;
  for (int i = 0; i < 2000; i++) x = x * 3 + (uint64_t)i;   /* ~15 us */
}

static void init_mem(void) {
    mem_buf = aligned_alloc(SLOT_SIZE, MEM_SIZE);
    if (!mem_buf) {
        perror("aligned_alloc");
        exit(1);
    }
    stream_idx = 0;
}

static void do_mem(void) {
    /* Dense streaming over a 256 MB buffer (larger than the 72 MB LLC):
       each new line is a capacity miss and evicts an existing line. */
    for (int i = 0; i < 1024; i++) {
        mem_buf[stream_idx]++;
        stream_idx += SLOT_SIZE;
        if (stream_idx >= MEM_SIZE)
            stream_idx = 0;
    }
}

static void maccess(const void *p) {
    asm volatile("movq (%0), %%rax\n" : : "c"(p) : "rax");
}

static void flushopt(const void *p) {
    asm volatile("clflushopt 0(%0)\n" : : "c"(p) : "rax");
}

static void init_crypto_map(void) {
    int fd = open(LIBCRYPTO_PATH, O_RDONLY);
    if (fd < 0) { perror("open libcrypto"); exit(1); }

    struct stat st;
    if (fstat(fd, &st) != 0) { perror("fstat libcrypto"); exit(1); }

    crypto_map = mmap(NULL, st.st_size, PROT_READ, MAP_SHARED, fd, 0);
    close(fd);
    if (crypto_map == MAP_FAILED) { perror("mmap libcrypto"); exit(1); }
}

static void do_target(void) {
    /* Touch the exact lines newspy watches so misses look like hits. */
    for (int i = 0; i < 1024; i++)
        for (size_t j = 0; j < N_PROBES; j++)
            maccess(crypto_map + PROBE_OFFSETS[j]);
}

static void do_evict(void) {
    /* Flush the exact lines newspy watches so hits look like misses. */
    for (int i = 0; i < 1024; i++)
        for (size_t j = 0; j < N_PROBES; j++)
            flushopt(crypto_map + PROBE_OFFSETS[j]);
}

int main(int argc, char **argv) {
  if (argc < 3) { fprintf(stderr, "Usage: %s {busy|mem|target|evict} DUTY_PCT\n", argv[0]); return 1; }
  int is_mem  = !strcmp(argv[1], "mem");
  int is_busy = !strcmp(argv[1], "busy");
  int is_target = !strcmp(argv[1], "target");
  int is_evict = !strcmp(argv[1], "evict");
  if (!is_mem && !is_busy && !is_target && !is_evict) {
      fprintf(stderr, "Mode must be busy|mem|target|evict\n");
      return 1;
  }
  int duty = atoi(argv[2]);
  if (duty < 0) duty = 0;
  if (duty > 100) duty = 100;

  if (is_mem)
    init_mem();
  if (is_target || is_evict)
    init_crypto_map();

  uint64_t on_ns  = CYCLE_NS * (uint64_t)duty / 100;
  uint64_t off_ns = CYCLE_NS - on_ns;

  for (;;) {
    uint64_t t0 = now_ns();
    while (now_ns() - t0 < on_ns) {
      if (is_mem) do_mem();
      else if (is_busy) do_busy();
      else if (is_target) do_target();
      else do_evict();
    }
    if (off_ns) {
      struct timespec ts = { off_ns / 1000000000ULL, off_ns % 1000000000ULL };
      nanosleep(&ts, NULL);
    }
  }
  return 0;
}
