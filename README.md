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

VIS is a deterministic performance platform for critical software on real CPU
and memory hardware. It measures hardware/OS noise, separates clean execution
from contaminated samples, and turns runtime behavior into evidence that can be
reported, compared, and eventually enforced.

It is **not** a magic accelerator. VIS does not claim to make every program
faster. Its claim is narrower and more useful:

> Critical software should not run on hardware blindly. It should run with a
> measured, explainable, and repeatable execution profile.

## What works today

VIS currently ships as a Linux/x86_64 prototype under `vis-jitter/`.

- `vis-jitter` measures CPU jitter with `RDTSCP`, rejects SMI-contaminated
  windows using `IA32_SMI_COUNT`, and emits terminal/JSON evidence.
- `vis-doctor` scans the machine, ranks CPU candidates, detects SMT, CPU
  governor, isolation, and HugePages signals, and writes an AI-readable
  diagnosis.
- `vis-run` reads Doctor policy, applies temporary workload CPU affinity, and
  emits a runtime attestation.
- `vis-compare` runs the same workload through VIS runtime profiles and can
  capture user-defined metrics such as `score`, `tok_s`, `loop_hz`, or
  `p99_ms`.

VIS is not a malware detector. It can, however, turn unexplained runtime,
latency, or throughput drift into reproducible evidence that may justify deeper
debugging, performance analysis, or security review.

Quick path:

```bash
cd vis-jitter
make
make test
sudo modprobe msr
sudo ./vis-doctor --scan --duration 30 --threshold 100 --output doctor.json --llm doctor.md
./vis-run --policy doctor.json --dry-run -- /bin/true
./vis-compare --policy doctor.json --metric score="score: ([0-9.]+)" -- ./your_program
```

---

## Architecture

| Layer | Component | What it does |
|---|---|---|
| Measurement | `vis-jitter` | Cycle-accurate jitter measurement, SMI audit, determinism report |
| Report | VIS Report | Stable JSON evidence format with detected vs asserted system facts |
| Diagnosis | VIS Doctor | Machine readiness and noise-source analysis for CPU/RAM determinism |
| CPU | VIS CPU | Pinning, isolation, IRQ affinity, SMT/governor/C-state profile validation |
| Memory | VIS-Mem | NUMA, HugePages, `mlock`, page fault, bandwidth, and cache-locality checks |
| Lab | VIS Lab | Profile comparison, before/after benchmark runs, and auto-tuning experiments |
| Inference | VIS-Infer | LLM/AI inference stability work on llama.cpp, OpenVINO, ONNX Runtime |
| Production | VIS Cell | Drift detection, CI/CD gates, signed attestation, dashboard/API |

---

## Roadmap

- **V1 — VIS Core / `vis-jitter`** ✅ Working proof — SMI-aware CPU jitter measurement and determinism reporting
- **V1.2 — VIS Report** 🔜 Harden the JSON report schema, public API, and reproducible evidence format
- **V1.3 — VIS Doctor** 🚧 Machine diagnosis and AI-readable recommendations: SMT, governor, IRQs, isolation, NUMA, HugePages, SMI access
- **V2 — VIS CPU** 🔜 Apply and verify low-risk CPU determinism profiles
- **V2.5 — VIS-Mem** 🔜 Measure and tune memory locality, page faults, HugePages, bandwidth, and cache behavior
- **V3 — VIS Lab** 🔜 Compare profiles, run before/after benchmarks, and recommend only measured improvements
- **V4 — VIS-Infer** 🔜 CPU/iGPU inference stability and throughput work for LLM/AI runtimes
- **V5 — VIS Cell** 🔜 Production drift detection, CI/CD performance gates, signed attestation, and enterprise workflow

Earlier research ideas such as compiler prefetch injection (`vis-opt`) and
specialized allocation libraries remain possible VIS CPU/Mem research tracks,
but the core roadmap is measurement → diagnosis → profile → verification.

---

## vis-jitter

The first and most fundamental component. Measures cycle-accurate latency using `RDTSCP` + `CPUID` serialization, monitors `IA32_SMI_COUNT` MSR to detect and reject contaminated measurement windows, and produces a structured determinism report.

### Why vis-jitter is different

Existing tools like `cyclictest` measure OS scheduling jitter. `vis-jitter` goes lower:

- Detects **SMI (System Management Interrupt)** events at Ring -2 — invisible to the OS
- Applies **full-window rejection** — if any SMI occurs in a window, all samples in that window are discarded
- Reports both contaminated windows and total `IA32_SMI_COUNT` delta, so rejection count and firmware counter movement are not confused
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

## VIS Doctor

VIS Doctor is the next layer above `vis-jitter`. It inspects the machine,
optionally runs all-core SMI-aware scans through the V1 measurement engine, and
emits both human-readable and AI-readable diagnostics.

```bash
cd vis-jitter
make vis-doctor

# Rootless inspection
./vis-doctor --inspect

# Root-required all-core scan with AI-friendly outputs
sudo ./vis-doctor --scan --duration 30 --threshold 100 --output doctor.json --llm doctor.md
```

The Markdown output is designed to be pasted into an AI assistant. VIS Doctor
V1.3 is advisory only: it explains, ranks, and recommends validation commands;
it does not change CPU, IRQ, governor, isolation, or memory settings.

---

## VIS Run

VIS Run is the first V2 runtime bridge. It reads the machine-readable
`recommended_runtime_policy` from `doctor.json`, applies a temporary CPU
affinity mask to one workload, and prints a small attestation when the workload
exits. It does not make persistent system changes.

```bash
cd vis-jitter
make vis-run

# Preview the policy without starting the workload
./vis-run --policy doctor.json --dry-run -- /bin/true

# Run a workload under the Doctor-recommended primary CPU policy
./vis-run --policy doctor.json --profile strict -- ./your_program

# Save a machine-readable run attestation for AI/review tools
./vis-run --policy doctor.json --output run-attestation.json -- ./your_program
```

V2.1 is still intentionally non-persistent, but it no longer stops at "affinity
was applied." During workload execution, `vis-run` samples `/proc` in
best-effort mode, records the observed process/thread CPU allowance, and reports
whether any observed thread was allowed outside the VIS-assigned CPU set. This is
not containment yet; it is escape detection and runtime evidence.

Failed actions are treated as evidence: if policy parsing, affinity application,
workload startup, or runtime observation detects a problem, `vis-run` reports the
failed/flagged step and prints the recommendation that becomes stronger because
of that evidence.
Use `--output` when you want to share the runtime evidence with another tool or
AI assistant; `doctor.json` is the prescription, `run-attestation.json` is the
record of what was actually attempted and observed.

For scripts and CI, `vis-run` passes through the workload result: a workload
that exits with code `1` makes `vis-run` exit with code `1`, and a failed
`execvp` path exits as `127`.

Planned hardening:

- thermal/throttle awareness before interpreting low accepted-sample throughput
- explicit `null`/omit semantics for unprovided asserted JSON fields
- cgroups v2 containment for child processes and forked workloads after the
  current best-effort escape detector

---

## VIS Compare Lite

VIS Compare Lite is the first small VIS Lab step. It runs the same workload
through `vis-run` with the Doctor-recommended `primary` and `secondary` CPU
sources, then summarizes the runtime evidence side by side.

```bash
cd vis-jitter
make vis-run vis-compare

./vis-compare --policy doctor.json \
  --metric score="score: ([0-9.]+)" \
  --output compare.json \
  --llm compare.md \
  -- ./your_program
```

VIS Compare always reports runtime-control evidence: assigned CPUs, exit code,
wall-clock duration, VIS Run verdict, affinity escapes, and warning count. When
`--metric name=regex` is provided, it also captures numeric values from the
workload/`vis-run` output and records them beside each profile. VIS does not
interpret the semantic meaning of a metric; it only compares user-requested
numbers such as `score`, `tok_s`, `loop_hz`, or `p99_ms`.

By default, per-profile output is captured under `vis-compare-runs/` and the
terminal stays focused on the comparison summary. Use `--show-output` when you
also want the captured output printed during the run. Full captured profile
output is available under the runs directory for optional review.

---

## Test Results

Three runs were performed to validate both baseline determinism and SMI detection under deliberate interference.

These are the original V1 proof runs. Current code derives nanoseconds from
the TSC conversion rate instead of CPU max frequency; fresh benchmark runs
should be captured before treating these numbers as final certification data.

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
   TSC freq.   : 4.800 GHz
   NUMA node   : 0
   SMT active  : no
   TSC invar.  : yes
   RDTSCP      : yes
----------------------------------------
 SMI audit
   Policy      : full_window
   Windows     : 0 contaminated
   MSR delta   : 0
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
- Zero contaminated SMI windows — measurement environment was clean
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
   TSC freq.   : 4.800 GHz
   NUMA node   : 0
   SMT active  : no
   TSC invar.  : yes
   RDTSCP      : yes
----------------------------------------
 SMI audit
   Policy      : full_window
   Windows     : 6 contaminated
   MSR delta   : 6
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
- 6 contaminated SMI windows detected — 6,000,000 samples automatically rejected
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
   TSC freq.   : 4.800 GHz
   NUMA node   : 0
   SMT active  : no
   TSC invar.  : yes
   RDTSCP      : yes
----------------------------------------
 SMI audit
   Policy      : full_window
   Windows     : 4 contaminated
   MSR delta   : 4
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
- 4 contaminated SMI windows detected — 4,000,000 samples rejected, report uncontaminated
- P99 consistent with Run 2 at 15 ns — rejection policy produces repeatable results
- P99.9 at 65 ns reflects OS scheduler noise without `isolcpus`, not SMI

---

### Summary across all runs

| Run | Duration | Contaminated windows | Rejected | P99 | P99.9 | Verdict |
|---|---|---|---|---|---|---|
| 1 — Baseline | 60s | 0 | 0 | 5.0 ns | 5.0 ns | PASS |
| 2 — SMI stress | 300s | 6 | 6,000,000 | 15.0 ns | 15.0 ns | PASS |
| 3 — SMI stress + | 60s | 4 | 4,000,000 | 15.0 ns | 65.0 ns | PASS |

**Conclusion:** `vis-jitter` consistently detects and rejects SMI-contaminated windows across all runs. P99 remains below threshold regardless of interference. The `full_window` rejection policy is validated as both correct and repeatable on commodity hardware.

---

### All-core exploratory scan — HP Victus 15-fa1xxx

A 30-second exploratory scan was run across all 20 online logical CPUs on the
same HP Victus system. This is not a certification dataset; it is an early
machine-local comparison showing how VIS reports per-core cleanliness and
throughput.

Command:

```bash
for cpu in $(seq 0 19); do
  sudo ./vis-jitter --cpu "$cpu" --duration 30 --threshold 100 --output "reports/core-${cpu}.json"
done
```

Summary:

| CPU range | Accepted samples | Contaminated windows | MSR delta | P99 | P99.9 | P99.99 | Verdict |
|---|---:|---:|---:|---:|---:|---:|---|
| 0–7, 9–11 | ~197M–198M | 0 | 0 | 15 ns | 15 ns | 15–25 ns | PASS |
| 8 | 179M | 3 | 4 | 15 ns | 95 ns | 105 ns | PASS |
| 12–19 | ~48M | 0 | 0 | 15 ns | 15 ns | 15 ns | PASS |

Key observations:

- Deliberate SMI interference was captured during the CPU 8 run: 3 contaminated windows, 4 total SMI counter increments, and 3,000,000 rejected samples.
- All logical CPUs passed the P99 <= 100 ns threshold.
- CPU 12–19 accepted far fewer samples in the same 30-second window, suggesting a lower-throughput core class on this hybrid CPU.
- Clean cores produced very similar latency percentiles because V1 uses 10 ns histogram buckets and the baseline workload is an empty serialized loop.
- `max = 5000 ns` should be read as "at or beyond the V1 histogram range"; exact overflow maxima are planned for a later report schema improvement.

This scan demonstrates the next natural direction for VIS Doctor: compare all
available CPUs, identify clean candidates, flag contaminated windows, and expose
core-class behavior through measured evidence instead of guessing.

---

### Controlled SMT sibling-load stress

After VIS Doctor began producing sibling-aware primary CPU candidates, a bounded
stress test was run to check whether busy SMT siblings changed the measured
behavior. This test intentionally kept the sibling logical CPUs busy with
`timeout` + `taskset` + `yes`, then measured CPU 0 and ran a 10-second Doctor
scan under the same kind of background load.

Safety note: this is a bounded stress test, not a thermal torture test. On
laptops, keep durations short and watch fan/temperature behavior.

Single-core comparison on CPU 0:

| Scenario | Background load | Accepted samples | P50 | P99 | P99.9 | P99.99 | SMI windows | Verdict |
|---|---|---:|---:|---:|---:|---:|---:|---|
| Clean CPU 0 | none | 394M | 5 ns | 15 ns | 15 ns | 15 ns | 0 | PASS |
| CPU 0 with sibling CPU 1 busy | `taskset -c 1 yes` | 338M | 15 ns | 15 ns | 15 ns | 15 ns | 0 | PASS |
| CPU 0 with siblings 1,3,5,7,9,11 busy | bounded sibling load | 340M | 15 ns | 15 ns | 25 ns | 35 ns | 0 | PASS |

Doctor scan under sibling-load:

| CPU class | Accepted/s under stress | P99 | P99.9 | SMI windows | Class |
|---|---:|---:|---:|---:|---|
| Primary candidates 0,2,4,6,8,10 | ~5.7M | 15 ns | 15–25 ns | 0 | higher-throughput |
| Busy sibling CPUs 1,3,5,7,9,11 | ~3.3M | 15 ns | 15 ns | 0 | higher-throughput |
| CPUs 12–19 | ~1.6M | 15 ns | 15 ns | 0 | lower-throughput |

Key observations:

- The controlled sibling load did not break the P99 latency verdict.
- No SMI-contaminated windows were observed.
- Clean accepted-sample throughput dropped from 394M to ~338M–340M on CPU 0.
- Tail latency moved under multi-sibling load: P99.9 rose from 15 ns to 25 ns and P99.99 rose to 35 ns.
- VIS Doctor preserved the same primary recommendation: `0,2,4,6,8,10`, while keeping `12–19` in the lower-throughput class.

This supports the VIS CPU policy design: SMT sibling sharing may not always
break P99 latency, but it can reduce clean execution capacity and move tail
latency. Strict runtime profiles should therefore prefer sibling-aware primary
CPU candidates first, then treat the full clean logical CPU list as secondary
evidence.

---

## License

MIT — see [LICENSE](LICENSE)

---

## Status

Early development. V1 (`vis-jitter`) is functional and tested on Linux x86-64.
See [VIS Core V1 Notes](V1_NOTES.md) for completed scope, known limits, and
validation commands.
Contributions welcome.
