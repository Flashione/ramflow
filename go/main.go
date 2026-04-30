package main

import (
	"flag"
	"fmt"
	"io"
	"os"
	"os/signal"
	"runtime"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"syscall"
	"time"
)

type config struct {
	size      uint64
	workers   int
	block     uint64
	pause     time.Duration
	duration  time.Duration
	status    time.Duration
	logPath   string
	touchOnly bool
}

type stats struct {
	rounds     atomic.Uint64
	bytesMoved atomic.Uint64
	guard      atomic.Uint64
}

func main() {
	cfg := parseFlags()

	perWorker := cfg.size / uint64(cfg.workers)
	if perWorker < 32*1024*1024 {
		exitError("too many workers for selected size")
	}
	perBuffer := perWorker / 2

	logWriter, closeLog := openLog(cfg.logPath)
	defer closeLog()

	printHeader(cfg, perWorker, perBuffer, logWriter)

	stop := atomic.Bool{}
	setupSignalHandler(&stop)

	workerStats := make([]*stats, cfg.workers)
	var wg sync.WaitGroup

	fmt.Println("Allocating and touching memory...")

	for id := 0; id < cfg.workers; id++ {
		st := &stats{}
		workerStats[id] = st
		wg.Add(1)
		go workerLoop(id, int(perBuffer), int(cfg.block), cfg.pause, cfg.touchOnly, &stop, st, &wg)
	}

	fmt.Println("Running. Press Ctrl+C to stop.")
	fmt.Println()

	start := time.Now()
	statusTicker := time.NewTicker(cfg.status)
	defer statusTicker.Stop()

	var durationTimer <-chan time.Time
	if cfg.duration > 0 {
		timer := time.NewTimer(cfg.duration)
		defer timer.Stop()
		durationTimer = timer.C
	}

	for !stop.Load() {
		select {
		case <-statusTicker.C:
			line := statusLine("running", time.Since(start), workerStats)
			fmt.Println(line)
			writeLine(logWriter, line)
		case <-durationTimer:
			stop.Store(true)
		case <-time.After(250 * time.Millisecond):
		}
	}

	wg.Wait()

	line := statusLine("stopped", time.Since(start), workerStats)
	fmt.Println()
	fmt.Println(line)
	writeLine(logWriter, line)

	runtime.KeepAlive(workerStats)
}

func parseFlags() config {
	sizeText := flag.String("size", "4G", "total RAM to allocate, for example 512M, 4G, 16G")
	workers := flag.Int("workers", 1, "number of worker goroutines")
	blockText := flag.String("block", "4M", "copy block size, for example 1M, 4M, 64M")
	pauseMs := flag.Uint64("pause-ms", 0, "pause after each copy round in milliseconds")
	durationText := flag.String("duration", "", "optional runtime duration, for example 30s, 10m, 2h")
	statusSec := flag.Uint64("status-sec", 10, "status interval in seconds")
	logPath := flag.String("log", "", "optional log file path")
	touchOnly := flag.Bool("touch-only", false, "touch allocated memory pages but do not copy between buffers")
	flag.Parse()

	size, err := parseSize(*sizeText)
	if err != nil {
		exitError(err.Error())
	}
	block, err := parseSize(*blockText)
	if err != nil {
		exitError(err.Error())
	}

	if *workers < 1 {
		exitError("--workers must be at least 1")
	}
	if size < 64*1024*1024 {
		exitError("--size should be at least 64M")
	}
	if block < 4096 {
		exitError("--block should be at least 4K")
	}
	if *statusSec == 0 {
		*statusSec = 1
	}

	var duration time.Duration
	if strings.TrimSpace(*durationText) != "" {
		var err error
		duration, err = time.ParseDuration(*durationText)
		if err != nil {
			exitError("invalid --duration, use values like 30s, 10m, 2h")
		}
	}

	return config{
		size:      size,
		workers:   *workers,
		block:     block,
		pause:     time.Duration(*pauseMs) * time.Millisecond,
		duration:  duration,
		status:    time.Duration(*statusSec) * time.Second,
		logPath:   *logPath,
		touchOnly: *touchOnly,
	}
}

func setupSignalHandler(stop *atomic.Bool) {
	sig := make(chan os.Signal, 1)
	signal.Notify(sig, os.Interrupt, syscall.SIGTERM)
	go func() {
		<-sig
		fmt.Println()
		fmt.Println("Stopping...")
		stop.Store(true)
	}()
}

func workerLoop(id int, bufferLen int, blockSize int, pause time.Duration, touchOnly bool, stop *atomic.Bool, st *stats, wg *sync.WaitGroup) {
	defer wg.Done()

	a := make([]byte, bufferLen)
	b := make([]byte, bufferLen)

	fillPattern(a, byte(id))
	touchMemory(b)

	for !stop.Load() {
		if touchOnly {
			guard := touchMemory(a) ^ touchMemory(b)
			st.guard.Add(guard)
			st.rounds.Add(1)
		} else {
			copyBlockwise(b, a, blockSize)
			copyBlockwise(a, b, blockSize)
			guard := sampleGuard(a) ^ sampleGuard(b)
			st.guard.Add(guard)
			st.rounds.Add(1)
			st.bytesMoved.Add(uint64(bufferLen) * 2)
		}

		if pause > 0 {
			time.Sleep(pause)
		}
	}

	runtime.KeepAlive(a)
	runtime.KeepAlive(b)
}

func copyBlockwise(dst []byte, src []byte, blockSize int) {
	if blockSize <= 0 || blockSize >= len(src) {
		copy(dst, src)
		return
	}

	for offset := 0; offset < len(src); offset += blockSize {
		end := offset + blockSize
		if end > len(src) {
			end = len(src)
		}
		copy(dst[offset:end], src[offset:end])
	}
}

func fillPattern(buf []byte, seed byte) {
	x := seed + 0x5a
	for i := range buf {
		x ^= x << 3
		x ^= x >> 5
		x += 17
		buf[i] = x
	}
}

func touchMemory(buf []byte) uint64 {
	guard := uint64(0)
	for i := 0; i < len(buf); i += 4096 {
		buf[i]++
		guard ^= uint64(buf[i])
	}
	if len(buf) > 0 {
		buf[len(buf)-1]++
		guard ^= uint64(buf[len(buf)-1])
	}
	return guard
}

func sampleGuard(buf []byte) uint64 {
	if len(buf) == 0 {
		return 0
	}
	step := len(buf) / 16
	if step < 1 {
		step = 1
	}
	guard := uint64(0)
	for i := 0; i < len(buf); i += step {
		guard = (guard << 5) | (guard >> 59)
		guard ^= uint64(buf[i])
	}
	guard ^= uint64(buf[len(buf)-1])
	return guard
}

func parseSize(input string) (uint64, error) {
	s := strings.TrimSpace(strings.ToUpper(input))
	if s == "" {
		return 0, fmt.Errorf("empty size")
	}

	split := len(s)
	for i, r := range s {
		if (r < '0' || r > '9') && r != '.' {
			split = i
			break
		}
	}

	number := s[:split]
	suffix := s[split:]
	value, err := strconv.ParseFloat(number, 64)
	if err != nil || value <= 0 {
		return 0, fmt.Errorf("invalid size: %s", input)
	}

	multiplier := uint64(1)
	switch suffix {
	case "", "B":
		multiplier = 1
	case "K", "KB", "KIB":
		multiplier = 1024
	case "M", "MB", "MIB":
		multiplier = 1024 * 1024
	case "G", "GB", "GIB":
		multiplier = 1024 * 1024 * 1024
	case "T", "TB", "TIB":
		multiplier = 1024 * 1024 * 1024 * 1024
	default:
		return 0, fmt.Errorf("unknown size suffix: %s", suffix)
	}

	return uint64(value * float64(multiplier)), nil
}

func printHeader(cfg config, perWorker uint64, perBuffer uint64, logWriter io.Writer) {
	lines := []string{
		"ramflow-go",
		"----------",
		"Mode:             simulated real-world RAM data movement",
		"Total allocation: " + formatBytes(cfg.size),
		fmt.Sprintf("Workers:          %d", cfg.workers),
		"Per worker:       " + formatBytes(perWorker),
		"Per buffer:       " + formatBytes(perBuffer),
		"Copy block:       " + formatBytes(cfg.block),
		fmt.Sprintf("Pause:            %d ms", cfg.pause.Milliseconds()),
		fmt.Sprintf("Touch only:       %t", cfg.touchOnly),
		"Duration:         " + formatDurationOrUntilStop(cfg.duration),
		"Log file:         " + logPathOrNone(cfg.logPath),
		"",
	}
	for _, line := range lines {
		fmt.Println(line)
		writeLine(logWriter, line)
	}
}

func statusLine(state string, elapsed time.Duration, workerStats []*stats) string {
	var rounds uint64
	var moved uint64
	var guard uint64
	for _, st := range workerStats {
		rounds += st.rounds.Load()
		moved += st.bytesMoved.Load()
		guard ^= st.guard.Load()
	}
	return fmt.Sprintf("%s %s elapsed=%s rounds=%d moved=%s guard=%d", timestamp(), state, formatDuration(elapsed), rounds, formatBytes(moved), guard)
}

func openLog(path string) (io.Writer, func()) {
	if path == "" {
		return io.Discard, func() {}
	}
	file, err := os.OpenFile(path, os.O_CREATE|os.O_APPEND|os.O_WRONLY, 0644)
	if err != nil {
		exitError("failed to open log file: " + err.Error())
	}
	return file, func() { _ = file.Close() }
}

func writeLine(w io.Writer, line string) {
	if w == nil || w == io.Discard {
		return
	}
	_, _ = fmt.Fprintln(w, line)
}

func timestamp() string {
	return time.Now().Format("2006-01-02 15:04:05")
}

func formatDurationOrUntilStop(d time.Duration) string {
	if d == 0 {
		return "until Ctrl+C"
	}
	return formatDuration(d)
}

func formatDuration(d time.Duration) string {
	seconds := int64(d.Seconds())
	h := seconds / 3600
	m := (seconds % 3600) / 60
	s := seconds % 60
	if h > 0 {
		return fmt.Sprintf("%dh %dm %ds", h, m, s)
	}
	if m > 0 {
		return fmt.Sprintf("%dm %ds", m, s)
	}
	return fmt.Sprintf("%ds", s)
}

func formatBytes(v uint64) string {
	const kib = 1024.0
	const mib = kib * 1024.0
	const gib = mib * 1024.0
	const tib = gib * 1024.0
	f := float64(v)
	if f >= tib {
		return fmt.Sprintf("%.2f TiB", f/tib)
	}
	if f >= gib {
		return fmt.Sprintf("%.2f GiB", f/gib)
	}
	if f >= mib {
		return fmt.Sprintf("%.2f MiB", f/mib)
	}
	if f >= kib {
		return fmt.Sprintf("%.2f KiB", f/kib)
	}
	return fmt.Sprintf("%d B", v)
}

func logPathOrNone(path string) string {
	if path == "" {
		return "none"
	}
	return path
}

func exitError(msg string) {
	fmt.Fprintln(os.Stderr, "error:", msg)
	os.Exit(2)
}
