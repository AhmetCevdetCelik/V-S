# VIS — Virtual Intelligent Scheduler

> *Reconciling Software and Silicon.*

## Manifesto

**The Great Decoupling**
Historically, abstraction was a liberating force. It broke the chains between software and specific hardware, enabling universal portability. But over the last few decades, this decoupling reached an extreme. Software ascended to ever-higher layers of abstraction while hardware became an underutilized, deeply misunderstood black box. This gap is where modern performance dies.

**The Cost of Abstraction**
The industry optimized for general-purpose speed, but in doing so, sacrificed determinism. Today's hardware is immensely powerful, yet its behavior is unpredictable due to the layers of bureaucracy between the algorithm and the transistor. VIS is the answer to this second act of the computer revolution.

**The Reconciliation**
Our mission is to reconcile these two worlds. VIS orchestrates the communication between the Compiler, the CPU, and the RAM — ensuring that software doesn't just run on hardware, but runs *with* it. We are making the transistors and the algorithms speak the same language again.

**The Proof**
VIS is the functional proof of this philosophy on the x86 architecture. By regaining control over the silicon, we move beyond just running code — we achieve atomic precision in execution.

---

## What is VIS?

VIS is a layered determinism toolkit for Intel x86 systems. It targets sub-100 ns P99 tail latency on high-priority threads by coordinating the compiler, OS, and hardware simultaneously.

It is **not** a single optimization. It is a full-stack discipline.

---

## Architecture

| Layer | Component | What it does |
|---|---|---|
| Compiler | `vis-opt` (V2) | Surgical `_mm_prefetch` injection via LLVM pass |
| OS | Kernel config | `isolcpus`, `nohz_full`, Intel CAT, HugePages |
| Hardware | `libvisalloc` (V3) | UC/WC hybrid memory allocation, NUMA pinning |
| Measurement | `vis-jitter` (V1) | Cycle-accurate latency measurement and SMI audit |

---

## Roadmap

- **V1 — `vis-jitter`** ✅ Released — SMI-aware jitter measurement and determinism reporting
- **V2 — `vis-opt`** 🔜 LLVM pass for surgical prefetch injection
- **V3 — `vis-stack`** 🔜 Full system integration, `libvisalloc`, market data demo

---

## vis-jitter

The first and most fundamental component. Measures cycle-accurate latency using `RDTSCP` + `CPUID` serialization, monitors `IA32_SMI_COUNT` MSR to detect and reject contaminated measurement windows, and produces a structured determinism report.

### Why vis-jitter is different

Existing tools like `cyclictest` measure OS scheduling jitter. `vis-jitter` goes lower:

- Detects **SMI (System Management Interrupt)** events at Ring -2 — invisible to the OS
- Applies **full-window rejection** — if any SMI occurs in a window, all samples in that window are discarded
- Separates **detected** system properties (measured by the tool) from **asserted** properties (user-supplied configuration)
- Produces a **structured JSON report** ready for audit and certification

### Build

```bash
# Dependencies
sudo apt install build-essential libnuma-dev

# Load MSR module
sudo modprobe msr

# Build
cd vis-jitter
make

# Run (requires root for MSR access)
sudo ./vis-jitter --cpu 2 --duration 60 --threshold 100

# Save JSON report
sudo ./vis-jitter --cpu 2 --duration 60 --threshold 100 --output report.json
```

### CLI options

| Flag | Default | Description |
|---|---|---|
| `--cpu` | 0 | CPU core to measure on |
| `--duration` | 60 | Measurement duration in seconds |
| `--threshold` | 100.0 | P99 pass threshold in nanoseconds |
| `--output` | — | Save JSON report to file |

---

## Test Results

Three runs were performed to validate both baseline determinism and SMI detection under deliberate interference.

---

### Run 1 — Baseline (60s, clean environment)

**Hardware:** HP Victus Gaming Laptop 15-fa1xxx
**CPU:** Intel Core (Alder Lake / Raptor Lake) @ 4.80 GHz
**Core:** Core 2, SMT off, NUMA node 0
**Note:** Standard kernel, no `isolcpus` applied. Baseline calibration — no deliberate interference.
**Duration:** 60 seconds | **Threshold:** 100 ns | **Workload:** Empty loop

```text
========================================
 vis-jitter report
========================================
 Generated : 2026-05-07T01:42:44Z
 Report ID : 36e97771-bf8a-4b57-af5d-00005bc22a69
----------------------------------------
 System (detected)
   Core        : 2
   Frequency   : 4.800 GHz
   NUMA node   : 0
   SMT active  : no
   TSC invar.  : yes
   RDTSCP      : yes
----------------------------------------
 SMI audit
   Policy      : full_window
   Events      : 0
   Rejected    : 0 samples
----------------------------------------
 Latency results
   Accepted    : 342,000,000 samples
   Core migr.  : 0 rejected
   min         : 0.0 ns
   p50         : 5.0 ns
   p99         : 5.0 ns
   p99.9       : 5.0 ns
   p99.99      : 5.0 ns
   max         : 4920.0 ns
----------------------------------------
 VERDICT: PASS — P99 5.0 ns <= threshold 100.0 ns
========================================
```

**Key insights:**
- Zero SMI events — measurement environment was clean
- P50 through P99.99 all at 5.0 ns — the core is a noise-free island under standard conditions
- The 4.9 µs max spike is OS scheduler interference, not SMI — expected without `isolcpus`

---

### Run 2 — SMI Stress Test (300s, deliberate interference)

**Hardware:** HP Victus Gaming Laptop 15-fa1xxx
**CPU:** Intel Core (Alder Lake / Raptor Lake) @ 4.80 GHz
**Core:** Core 2, SMT off, NUMA node 0
**Note:** SMI events deliberately triggered during measurement to validate `full_window` rejection policy.
**Duration:** 300 seconds | **Threshold:** 100 ns | **Workload:** Empty loop

```text
========================================
 vis-jitter report
========================================
 Generated : 2026-05-07T02:31:50Z
 Report ID : 49c74c49-2959-45e7-aaf6-00004083d70c
----------------------------------------
 System (detected)
   Core        : 2
   Frequency   : 4.800 GHz
   NUMA node   : 0
   SMT active  : no
   TSC invar.  : yes
   RDTSCP      : yes
----------------------------------------
 SMI audit
   Policy      : full_window
   Events      : 6
   Rejected    : 6,000,000 samples
----------------------------------------
 Latency results
   Accepted    : 1,886,000,000 samples
   Core migr.  : 0 rejected
   min         : 0.0 ns
   p50         : 5.0 ns
   p99         : 15.0 ns
   p99.9       : 15.0 ns
   p99.99      : 65.0 ns
   max         : 5000.0 ns
----------------------------------------
 VERDICT: PASS — P99 15.0 ns <= threshold 100.0 ns
========================================
```

**Key insights:**
- 6 SMI events detected — 6,000,000 contaminated samples automatically rejected
- Despite deliberate interference, P99 held at 15 ns — well within threshold
- 1.886 billion clean samples accepted — statistically robust result across 5 minutes

---

### Run 3 — SMI Stress Test (60s, repeated validation)

**Hardware:** HP Victus Gaming Laptop 15-fa1xxx
**CPU:** Intel Core (Alder Lake / Raptor Lake) @ 4.80 GHz
**Core:** Core 2, SMT off, NUMA node 0
**Note:** Second deliberate SMI interference run, shorter duration. Confirms rejection policy is consistent across runs.
**Duration:** 60 seconds | **Threshold:** 100 ns | **Workload:** Empty loop

```text
========================================
 vis-jitter report
========================================
 Generated : 2026-05-07T03:17:04Z
 Report ID : 43cca583-dfc6-467c-95b4-0000124c86b3
----------------------------------------
 System (detected)
   Core        : 2
   Frequency   : 4.800 GHz
   NUMA node   : 0
   SMT active  : no
   TSC invar.  : yes
   RDTSCP      : yes
----------------------------------------
 SMI audit
   Policy      : full_window
   Events      : 4
   Rejected    : 4,000,000 samples
----------------------------------------
 Latency results
   Accepted    : 322,000,000 samples
   Core migr.  : 0 rejected
   min         : 0.0 ns
   p50         : 5.0 ns
   p99         : 15.0 ns
   p99.9       : 65.0 ns
   p99.99      : 65.0 ns
   max         : 3220.0 ns
----------------------------------------
 VERDICT: PASS — P99 15.0 ns <= threshold 100.0 ns
========================================
```

**Key insights:**
- 4 SMI events detected — 4,000,000 samples rejected, report uncontaminated
- P99 consistent with Run 2 at 15 ns — rejection policy produces repeatable results
- P99.9 at 65 ns reflects OS scheduler noise without `isolcpus`, not SMI

---

### Summary across all runs

| Run | Duration | SMI Events | Rejected | P99 | P99.9 | Verdict |
|---|---|---|---|---|---|---|
| 1 — Baseline | 60s | 0 | 0 | 5.0 ns | 5.0 ns | PASS |
| 2 — SMI stress | 300s | 6 | 6,000,000 | 15.0 ns | 15.0 ns | PASS |
| 3 — SMI stress + | 60s | 4 | 4,000,000 | 15.0 ns | 65.0 ns | PASS |

**Conclusion:** `vis-jitter` consistently detects and rejects SMI-contaminated windows across all runs. P99 remains below threshold regardless of interference. The `full_window` rejection policy is validated as both correct and repeatable on commodity hardware.

---

## License

MIT — see [LICENSE](LICENSE)

---

## Status

Early development. V1 (`vis-jitter`) is functional and tested on Linux x86-64.
Contributions welcome.
