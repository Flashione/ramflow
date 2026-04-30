#define _GNU_SOURCE

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t g_stop = 0;

typedef struct {
    int id;
    uint8_t *a;
    uint8_t *b;
    size_t len;
    size_t block;
    unsigned pause_ms;
    int touch_only;
    atomic_uint_fast64_t rounds;
    atomic_uint_fast64_t bytes_moved;
    atomic_uint_fast64_t guard;
} worker_t;

typedef struct {
    uint64_t size;
    int workers;
    uint64_t block;
    unsigned pause_ms;
    unsigned duration_sec;
    unsigned status_sec;
    int touch_only;
} config_t;

static void on_signal(int sig) {
    (void)sig;
    g_stop = 1;
}

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

static void sleep_ms(unsigned ms) {
    if (ms > 0) {
        usleep((useconds_t)ms * 1000U);
    }
}

static void exit_error(const char *msg) {
    fprintf(stderr, "error: %s\n", msg);
    exit(2);
}

static uint64_t parse_size(const char *s) {
    if (!s || !*s) {
        exit_error("empty size");
    }

    char *end = NULL;
    double value = strtod(s, &end);
    if (end == s || value <= 0.0) {
        exit_error("invalid size");
    }

    uint64_t multiplier = 1;
    if (*end != '\0') {
        if (!strcasecmp(end, "K") || !strcasecmp(end, "KB") || !strcasecmp(end, "KIB")) {
            multiplier = 1024ULL;
        } else if (!strcasecmp(end, "M") || !strcasecmp(end, "MB") || !strcasecmp(end, "MIB")) {
            multiplier = 1024ULL * 1024ULL;
        } else if (!strcasecmp(end, "G") || !strcasecmp(end, "GB") || !strcasecmp(end, "GIB")) {
            multiplier = 1024ULL * 1024ULL * 1024ULL;
        } else if (!strcasecmp(end, "T") || !strcasecmp(end, "TB") || !strcasecmp(end, "TIB")) {
            multiplier = 1024ULL * 1024ULL * 1024ULL * 1024ULL;
        } else {
            exit_error("unknown size suffix");
        }
    }

    return (uint64_t)(value * (double)multiplier);
}

static void format_bytes(uint64_t bytes, char *out, size_t out_len) {
    const double kib = 1024.0;
    const double mib = kib * 1024.0;
    const double gib = mib * 1024.0;
    const double tib = gib * 1024.0;
    double v = (double)bytes;

    if (v >= tib) {
        snprintf(out, out_len, "%.2f TiB", v / tib);
    } else if (v >= gib) {
        snprintf(out, out_len, "%.2f GiB", v / gib);
    } else if (v >= mib) {
        snprintf(out, out_len, "%.2f MiB", v / mib);
    } else if (v >= kib) {
        snprintf(out, out_len, "%.2f KiB", v / kib);
    } else {
        snprintf(out, out_len, "%llu B", (unsigned long long)bytes);
    }
}

static void fill_pattern(uint8_t *buf, size_t len, uint8_t seed) {
    uint8_t x = (uint8_t)(seed + 0x5aU);
    for (size_t i = 0; i < len; i++) {
        x ^= (uint8_t)(x << 3);
        x ^= (uint8_t)(x >> 5);
        x = (uint8_t)(x + 17U);
        buf[i] = x;
    }
}

static uint64_t touch_memory(uint8_t *buf, size_t len) {
    const size_t page = 4096;
    uint64_t guard = 0;

    for (size_t i = 0; i < len; i += page) {
        buf[i] = (uint8_t)(buf[i] + 1U);
        guard ^= buf[i];
    }

    if (len > 0) {
        buf[len - 1] = (uint8_t)(buf[len - 1] + 1U);
        guard ^= buf[len - 1];
    }

    return guard;
}

static uint64_t sample_guard(const uint8_t *buf, size_t len) {
    if (len == 0) {
        return 0;
    }

    size_t step = len / 16;
    if (step == 0) {
        step = 1;
    }

    uint64_t guard = 0;
    for (size_t i = 0; i < len; i += step) {
        guard = (guard << 5) | (guard >> 59);
        guard ^= buf[i];
    }
    guard ^= buf[len - 1];
    return guard;
}

static void copy_blockwise(uint8_t *dst, const uint8_t *src, size_t len, size_t block) {
    if (block == 0 || block >= len) {
        memcpy(dst, src, len);
        return;
    }

    for (size_t offset = 0; offset < len; offset += block) {
        size_t end = offset + block;
        if (end > len) {
            end = len;
        }
        memcpy(dst + offset, src + offset, end - offset);
    }
}

static void *worker_main(void *arg) {
    worker_t *w = (worker_t *)arg;

    w->a = malloc(w->len);
    w->b = malloc(w->len);
    if (!w->a || !w->b) {
        fprintf(stderr, "worker %d: allocation failed: %s\n", w->id, strerror(errno));
        g_stop = 1;
        return NULL;
    }

    fill_pattern(w->a, w->len, (uint8_t)w->id);
    touch_memory(w->b, w->len);

    while (!g_stop) {
        if (w->touch_only) {
            uint64_t guard = touch_memory(w->a, w->len) ^ touch_memory(w->b, w->len);
            atomic_fetch_xor_explicit(&w->guard, guard, memory_order_relaxed);
            atomic_fetch_add_explicit(&w->rounds, 1, memory_order_relaxed);
        } else {
            copy_blockwise(w->b, w->a, w->len, w->block);
            copy_blockwise(w->a, w->b, w->len, w->block);
            uint64_t guard = sample_guard(w->a, w->len) ^ sample_guard(w->b, w->len);
            atomic_fetch_xor_explicit(&w->guard, guard, memory_order_relaxed);
            atomic_fetch_add_explicit(&w->rounds, 1, memory_order_relaxed);
            atomic_fetch_add_explicit(&w->bytes_moved, (uint64_t)w->len * 2ULL, memory_order_relaxed);
        }

        sleep_ms(w->pause_ms);
    }

    free(w->a);
    free(w->b);
    return NULL;
}

static void usage(const char *prog) {
    printf("Usage: %s [options]\n", prog);
    printf("\n");
    printf("ramflow-c simulates real-world RAM data movement.\n");
    printf("It is not a benchmark, not a stress test and not a replacement for MemTest86 or memtest86+.\n");
    printf("\n");
    printf("Options:\n");
    printf("  --size <SIZE>        Total RAM to allocate, e.g. 512M, 4G, 16G\n");
    printf("  --workers <N>        Number of worker threads, default: 1\n");
    printf("  --block <SIZE>       Copy block size, default: 4M\n");
    printf("  --pause-ms <N>       Pause after each copy round, default: 0\n");
    printf("  --duration <SECONDS> Optional runtime in seconds, default: until Ctrl+C\n");
    printf("  --status-sec <N>     Status interval in seconds, default: 10\n");
    printf("  --touch-only         Touch pages but do not copy between buffers\n");
    printf("  -h, --help           Show this help\n");
}

static config_t parse_args(int argc, char **argv) {
    config_t cfg;
    cfg.size = parse_size("4G");
    cfg.workers = 1;
    cfg.block = parse_size("4M");
    cfg.pause_ms = 0;
    cfg.duration_sec = 0;
    cfg.status_sec = 10;
    cfg.touch_only = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--size") && i + 1 < argc) {
            cfg.size = parse_size(argv[++i]);
        } else if (!strcmp(argv[i], "--workers") && i + 1 < argc) {
            cfg.workers = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--block") && i + 1 < argc) {
            cfg.block = parse_size(argv[++i]);
        } else if (!strcmp(argv[i], "--pause-ms") && i + 1 < argc) {
            cfg.pause_ms = (unsigned)strtoul(argv[++i], NULL, 10);
        } else if (!strcmp(argv[i], "--duration") && i + 1 < argc) {
            cfg.duration_sec = (unsigned)strtoul(argv[++i], NULL, 10);
        } else if (!strcmp(argv[i], "--status-sec") && i + 1 < argc) {
            cfg.status_sec = (unsigned)strtoul(argv[++i], NULL, 10);
        } else if (!strcmp(argv[i], "--touch-only")) {
            cfg.touch_only = 1;
        } else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            usage(argv[0]);
            exit(0);
        } else {
            fprintf(stderr, "unknown or incomplete option: %s\n", argv[i]);
            usage(argv[0]);
            exit(2);
        }
    }

    if (cfg.workers < 1) {
        exit_error("--workers must be at least 1");
    }
    if (cfg.size < 64ULL * 1024ULL * 1024ULL) {
        exit_error("--size should be at least 64M");
    }
    if (cfg.block < 4096) {
        exit_error("--block should be at least 4K");
    }
    if (cfg.status_sec == 0) {
        cfg.status_sec = 1;
    }

    return cfg;
}

int main(int argc, char **argv) {
    config_t cfg = parse_args(argc, argv);

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    uint64_t per_worker = cfg.size / (uint64_t)cfg.workers;
    if (per_worker < 32ULL * 1024ULL * 1024ULL) {
        exit_error("too many workers for selected size");
    }
    uint64_t per_buffer = per_worker / 2ULL;

    char total_s[64], per_worker_s[64], per_buffer_s[64], block_s[64];
    format_bytes(cfg.size, total_s, sizeof(total_s));
    format_bytes(per_worker, per_worker_s, sizeof(per_worker_s));
    format_bytes(per_buffer, per_buffer_s, sizeof(per_buffer_s));
    format_bytes(cfg.block, block_s, sizeof(block_s));

    printf("ramflow-c\n");
    printf("---------\n");
    printf("Mode:             simulated real-world RAM data movement\n");
    printf("Total allocation: %s\n", total_s);
    printf("Workers:          %d\n", cfg.workers);
    printf("Per worker:       %s\n", per_worker_s);
    printf("Per buffer:       %s\n", per_buffer_s);
    printf("Copy block:       %s\n", block_s);
    printf("Pause:            %u ms\n", cfg.pause_ms);
    printf("Touch only:       %s\n", cfg.touch_only ? "true" : "false");
    printf("Duration:         %s\n", cfg.duration_sec ? "limited" : "until Ctrl+C");
    printf("\nAllocating and touching memory...\n");

    worker_t *workers = calloc((size_t)cfg.workers, sizeof(worker_t));
    pthread_t *threads = calloc((size_t)cfg.workers, sizeof(pthread_t));
    if (!workers || !threads) {
        exit_error("failed to allocate worker metadata");
    }

    for (int i = 0; i < cfg.workers; i++) {
        workers[i].id = i;
        workers[i].len = (size_t)per_buffer;
        workers[i].block = (size_t)cfg.block;
        workers[i].pause_ms = cfg.pause_ms;
        workers[i].touch_only = cfg.touch_only;
        atomic_init(&workers[i].rounds, 0);
        atomic_init(&workers[i].bytes_moved, 0);
        atomic_init(&workers[i].guard, 0);

        if (pthread_create(&threads[i], NULL, worker_main, &workers[i]) != 0) {
            fprintf(stderr, "failed to create worker %d\n", i);
            g_stop = 1;
            cfg.workers = i;
            break;
        }
    }

    printf("Running. Press Ctrl+C to stop.\n\n");

    double start = now_sec();
    double last_status = start;

    while (!g_stop) {
        sleep_ms(250);
        double now = now_sec();

        if (cfg.duration_sec > 0 && now - start >= (double)cfg.duration_sec) {
            g_stop = 1;
            break;
        }

        if (now - last_status >= (double)cfg.status_sec) {
            uint64_t rounds = 0, moved = 0, guard = 0;
            for (int i = 0; i < cfg.workers; i++) {
                rounds += atomic_load_explicit(&workers[i].rounds, memory_order_relaxed);
                moved += atomic_load_explicit(&workers[i].bytes_moved, memory_order_relaxed);
                guard ^= atomic_load_explicit(&workers[i].guard, memory_order_relaxed);
            }

            char moved_s[64];
            format_bytes(moved, moved_s, sizeof(moved_s));
            printf("elapsed=%.0fs running rounds=%llu moved=%s guard=%llu\n",
                   now - start,
                   (unsigned long long)rounds,
                   moved_s,
                   (unsigned long long)guard);
            fflush(stdout);
            last_status = now;
        }
    }

    for (int i = 0; i < cfg.workers; i++) {
        pthread_join(threads[i], NULL);
    }

    uint64_t rounds = 0, moved = 0, guard = 0;
    for (int i = 0; i < cfg.workers; i++) {
        rounds += atomic_load_explicit(&workers[i].rounds, memory_order_relaxed);
        moved += atomic_load_explicit(&workers[i].bytes_moved, memory_order_relaxed);
        guard ^= atomic_load_explicit(&workers[i].guard, memory_order_relaxed);
    }

    char moved_s[64];
    format_bytes(moved, moved_s, sizeof(moved_s));
    printf("\nelapsed=%.0fs stopped rounds=%llu moved=%s guard=%llu\n",
           now_sec() - start,
           (unsigned long long)rounds,
           moved_s,
           (unsigned long long)guard);

    free(workers);
    free(threads);
    return 0;
}
