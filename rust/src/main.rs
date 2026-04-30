use chrono::Local;
use clap::Parser;
use std::fs::{File, OpenOptions};
use std::hint::black_box;
use std::io::{BufWriter, Write};
use std::process;
use std::sync::{atomic::{AtomicBool, AtomicU64, Ordering}, Arc};
use std::thread;
use std::time::{Duration, Instant};

#[derive(Parser, Debug, Clone)]
#[command(
    name = "ramflow",
    version,
    about = "Simulated real-world RAM data movement tool",
    long_about = "ramflow allocates memory and continuously moves data between buffers. It is not a benchmark, not a stress test and not a replacement for MemTest86 or memtest86+."
)]
struct Args {
    /// Total memory to allocate, for example 512M, 4G, 16G
    #[arg(long, default_value = "4G")]
    size: String,

    /// Number of worker threads
    #[arg(long, default_value_t = 1)]
    workers: usize,

    /// Copy block size, for example 1M, 4M, 64M
    #[arg(long, default_value = "4M")]
    block: String,

    /// Pause after each copy round in milliseconds
    #[arg(long, default_value_t = 0)]
    pause_ms: u64,

    /// Optional runtime duration, for example 30s, 10m, 2h
    #[arg(long)]
    duration: Option<String>,

    /// Status interval in seconds
    #[arg(long, default_value_t = 10)]
    status_sec: u64,

    /// Optional log file path
    #[arg(long)]
    log: Option<String>,

    /// Touch allocated memory pages but do not copy between buffers
    #[arg(long, default_value_t = false)]
    touch_only: bool,
}

struct WorkerStats {
    rounds: AtomicU64,
    bytes_moved: AtomicU64,
    guard: AtomicU64,
}

impl WorkerStats {
    fn new() -> Self {
        Self {
            rounds: AtomicU64::new(0),
            bytes_moved: AtomicU64::new(0),
            guard: AtomicU64::new(0),
        }
    }
}

fn main() {
    let args = Args::parse();

    if args.workers == 0 {
        exit_with_error("--workers must be at least 1");
    }

    let total_size = parse_size(&args.size).unwrap_or_else(|e| exit_with_error(&e));
    let block_size = parse_size(&args.block).unwrap_or_else(|e| exit_with_error(&e));

    if total_size < 64 * 1024 * 1024 {
        exit_with_error("--size should be at least 64M");
    }

    if block_size < 4096 {
        exit_with_error("--block should be at least 4K");
    }

    let duration = args.duration.as_ref().map(|s| {
        humantime::parse_duration(s)
            .unwrap_or_else(|_| exit_with_error("invalid --duration, use values like 30s, 10m, 2h"))
    });

    let per_worker = total_size / args.workers as u64;
    if per_worker < 32 * 1024 * 1024 {
        exit_with_error("too many workers for selected size");
    }

    let per_buffer = per_worker / 2;
    if per_buffer > usize::MAX as u64 {
        exit_with_error("buffer too large for this platform");
    }

    let stop = Arc::new(AtomicBool::new(false));
    {
        let stop = Arc::clone(&stop);
        ctrlc::set_handler(move || {
            eprintln!("\nStopping...");
            stop.store(true, Ordering::SeqCst);
        })
        .unwrap_or_else(|e| {
            eprintln!("error: failed to set Ctrl+C handler: {e}");
            process::exit(1);
        });
    }

    let mut log = open_log(args.log.as_deref());

    print_header(&args, total_size, per_worker, per_buffer, block_size, duration);
    write_log_header(&mut log, &args, total_size, per_worker, per_buffer, block_size, duration);

    let mut handles = Vec::with_capacity(args.workers);
    let mut stats_list = Vec::with_capacity(args.workers);

    println!("Allocating and touching memory...");

    for id in 0..args.workers {
        let stats = Arc::new(WorkerStats::new());
        stats_list.push(Arc::clone(&stats));

        let handle = {
            let stop = Arc::clone(&stop);
            let block_size = block_size as usize;
            let pause = Duration::from_millis(args.pause_ms);
            let touch_only = args.touch_only;
            let buffer_len = per_buffer as usize;

            thread::spawn(move || worker_loop(id, buffer_len, block_size, pause, touch_only, stop, stats))
        };

        handles.push(handle);
    }

    println!("Running. Press Ctrl+C to stop.\n");

    let start = Instant::now();
    let status_interval = Duration::from_secs(args.status_sec.max(1));
    let mut last_status = Instant::now();

    loop {
        thread::sleep(Duration::from_millis(250));

        if duration.is_some_and(|limit| start.elapsed() >= limit) {
            stop.store(true, Ordering::SeqCst);
        }

        if stop.load(Ordering::SeqCst) {
            break;
        }

        if last_status.elapsed() >= status_interval {
            let line = status_line(start.elapsed(), &stats_list, "running");
            println!("{line}");
            write_log_line(&mut log, &line);
            last_status = Instant::now();
        }
    }

    for handle in handles {
        if let Err(e) = handle.join() {
            eprintln!("warning: worker thread failed: {e:?}");
        }
    }

    let final_line = status_line(start.elapsed(), &stats_list, "stopped");
    println!("\n{final_line}");
    write_log_line(&mut log, &final_line);
}

fn worker_loop(
    id: usize,
    buffer_len: usize,
    block_size: usize,
    pause: Duration,
    touch_only: bool,
    stop: Arc<AtomicBool>,
    stats: Arc<WorkerStats>,
) {
    let mut a = vec![0u8; buffer_len];
    let mut b = vec![0u8; buffer_len];

    fill_pattern(&mut a, id as u8);
    touch_memory(&mut b);

    while !stop.load(Ordering::Relaxed) {
        if touch_only {
            let guard = touch_memory(&mut a) ^ touch_memory(&mut b);
            black_box(guard);
            stats.guard.fetch_xor(guard, Ordering::Relaxed);
            stats.rounds.fetch_add(1, Ordering::Relaxed);
        } else {
            copy_blockwise(&mut b, &a, block_size);
            copy_blockwise(&mut a, &b, block_size);
            let guard = sample_guard(&a) ^ sample_guard(&b);
            black_box(guard);
            stats.guard.fetch_xor(guard, Ordering::Relaxed);
            stats.rounds.fetch_add(1, Ordering::Relaxed);
            stats.bytes_moved.fetch_add((buffer_len as u64) * 2, Ordering::Relaxed);
        }

        if !pause.is_zero() {
            thread::sleep(pause);
        }
    }

    black_box(a);
    black_box(b);
}

fn copy_blockwise(dst: &mut [u8], src: &[u8], block_size: usize) {
    if block_size >= src.len() {
        dst.copy_from_slice(src);
        return;
    }

    let mut offset = 0;
    while offset < src.len() {
        let end = (offset + block_size).min(src.len());
        dst[offset..end].copy_from_slice(&src[offset..end]);
        offset = end;
    }
}

fn fill_pattern(buf: &mut [u8], seed: u8) {
    let mut x = seed.wrapping_add(0x5a);
    for byte in buf.iter_mut() {
        x ^= x.wrapping_shl(3);
        x ^= x.wrapping_shr(5);
        x = x.wrapping_add(17);
        *byte = x;
    }
}

fn touch_memory(buf: &mut [u8]) -> u64 {
    let page = 4096usize;
    let mut guard = 0u64;
    let mut i = 0usize;

    while i < buf.len() {
        buf[i] = buf[i].wrapping_add(1);
        guard ^= buf[i] as u64;
        i += page;
    }

    if let Some(last) = buf.last_mut() {
        *last = last.wrapping_add(1);
        guard ^= *last as u64;
    }

    guard
}

fn sample_guard(buf: &[u8]) -> u64 {
    if buf.is_empty() {
        return 0;
    }

    let step = (buf.len() / 16).max(1);
    let mut guard = 0u64;
    let mut i = 0usize;

    while i < buf.len() {
        guard = guard.rotate_left(5) ^ buf[i] as u64;
        i += step;
    }

    guard ^ buf[buf.len() - 1] as u64
}

fn parse_size(input: &str) -> Result<u64, String> {
    let s = input.trim().to_ascii_uppercase();
    if s.is_empty() {
        return Err("empty size".into());
    }

    let split_at = s.find(|c: char| !(c.is_ascii_digit() || c == '.')).unwrap_or(s.len());
    let (number, suffix) = s.split_at(split_at);

    let value: f64 = number.parse().map_err(|_| format!("invalid size number: {input}"))?;
    if value <= 0.0 {
        return Err("size must be greater than zero".into());
    }

    let multiplier = match suffix {
        "" | "B" => 1u64,
        "K" | "KB" | "KIB" => 1024u64,
        "M" | "MB" | "MIB" => 1024u64.pow(2),
        "G" | "GB" | "GIB" => 1024u64.pow(3),
        "T" | "TB" | "TIB" => 1024u64.pow(4),
        _ => return Err(format!("unknown size suffix: {suffix}")),
    };

    Ok((value * multiplier as f64) as u64)
}

fn open_log(path: Option<&str>) -> Option<BufWriter<File>> {
    path.map(|path| {
        let file = OpenOptions::new().create(true).append(true).open(path).unwrap_or_else(|e| {
            eprintln!("error: failed to open log file '{path}': {e}");
            process::exit(1);
        });
        BufWriter::new(file)
    })
}

fn print_header(args: &Args, total_size: u64, per_worker: u64, per_buffer: u64, block_size: u64, duration: Option<Duration>) {
    println!("ramflow");
    println!("-------");
    println!("Mode:             simulated real-world RAM data movement");
    println!("Total allocation: {}", format_bytes(total_size));
    println!("Workers:          {}", args.workers);
    println!("Per worker:       {}", format_bytes(per_worker));
    println!("Per buffer:       {}", format_bytes(per_buffer));
    println!("Copy block:       {}", format_bytes(block_size));
    println!("Pause:            {} ms", args.pause_ms);
    println!("Touch only:       {}", args.touch_only);
    println!("Duration:         {}", duration.map(format_duration).unwrap_or_else(|| "until Ctrl+C".to_string()));
    println!("Log file:         {}", args.log.as_deref().unwrap_or("none"));
    println!();
}

fn write_log_header(log: &mut Option<BufWriter<File>>, args: &Args, total_size: u64, per_worker: u64, per_buffer: u64, block_size: u64, duration: Option<Duration>) {
    let Some(log) = log else { return; };
    let _ = writeln!(log, "{} start", timestamp());
    let _ = writeln!(log, "mode=simulated real-world RAM data movement");
    let _ = writeln!(log, "total_allocation={}", format_bytes(total_size));
    let _ = writeln!(log, "workers={}", args.workers);
    let _ = writeln!(log, "per_worker={}", format_bytes(per_worker));
    let _ = writeln!(log, "per_buffer={}", format_bytes(per_buffer));
    let _ = writeln!(log, "copy_block={}", format_bytes(block_size));
    let _ = writeln!(log, "pause_ms={}", args.pause_ms);
    let _ = writeln!(log, "touch_only={}", args.touch_only);
    let _ = writeln!(log, "duration={}", duration.map(format_duration).unwrap_or_else(|| "until Ctrl+C".to_string()));
    let _ = log.flush();
}

fn write_log_line(log: &mut Option<BufWriter<File>>, line: &str) {
    if let Some(log) = log {
        let _ = writeln!(log, "{line}");
        let _ = log.flush();
    }
}

fn status_line(elapsed: Duration, stats_list: &[Arc<WorkerStats>], state: &str) -> String {
    let (rounds, bytes, guard) = collect_stats(stats_list);
    format!(
        "{} {} elapsed={} rounds={} moved={} guard={}",
        timestamp(),
        state,
        format_duration(elapsed),
        rounds,
        format_bytes(bytes),
        guard
    )
}

fn collect_stats(stats_list: &[Arc<WorkerStats>]) -> (u64, u64, u64) {
    let mut rounds = 0;
    let mut bytes = 0;
    let mut guard = 0;

    for stats in stats_list {
        rounds += stats.rounds.load(Ordering::Relaxed);
        bytes += stats.bytes_moved.load(Ordering::Relaxed);
        guard ^= stats.guard.load(Ordering::Relaxed);
    }

    (rounds, bytes, guard)
}

fn timestamp() -> String {
    Local::now().format("%Y-%m-%d %H:%M:%S").to_string()
}

fn format_duration(d: Duration) -> String {
    let secs = d.as_secs();
    let h = secs / 3600;
    let m = (secs % 3600) / 60;
    let s = secs % 60;

    if h > 0 {
        format!("{h}h {m}m {s}s")
    } else if m > 0 {
        format!("{m}m {s}s")
    } else {
        format!("{s}s")
    }
}

fn format_bytes(v: u64) -> String {
    const KIB: f64 = 1024.0;
    const MIB: f64 = KIB * 1024.0;
    const GIB: f64 = MIB * 1024.0;
    const TIB: f64 = GIB * 1024.0;
    let f = v as f64;

    if f >= TIB {
        format!("{:.2} TiB", f / TIB)
    } else if f >= GIB {
        format!("{:.2} GiB", f / GIB)
    } else if f >= MIB {
        format!("{:.2} MiB", f / MIB)
    } else if f >= KIB {
        format!("{:.2} KiB", f / KIB)
    } else {
        format!("{v} B")
    }
}

fn exit_with_error(msg: &str) -> ! {
    eprintln!("error: {msg}");
    process::exit(2);
}
