# VIS Core V1 Notes

VIS Core V1 is the first working proof of the VIS idea: critical software
should run on hardware with measurable, explainable, and repeatable evidence.

## Completed in V1

- `vis-jitter` measures serialized empty-loop latency with `RDTSCP` + `CPUID`.
- TSC cycle deltas are converted with a derived/calibrated TSC frequency, not CPU max frequency.
- The SMI audit reads `IA32_SMI_COUNT` and rejects full contaminated windows.
- Reports distinguish detected system facts from asserted user claims.
- JSON reports are escaped, parseable, and include SMI `contaminated_windows` and `msr_delta`.
- CLI argument parsing rejects malformed CPU, duration, and threshold values.
- CPU pinning validates CPU IDs before using `CPU_SET`.
- Histogram percentiles use nearest-rank semantics and handle sparse/overflow samples.
- The public `vis_jitter_run()` API is implemented.
- README includes baseline, SMI stress, and all-core exploratory scan results.

## Known V1 Limits

- `max = 5000 ns` means the sample reached or exceeded the V1 histogram range; exact overflow maxima are not tracked yet.
- `events_detected` is kept for backward compatibility and aliases `contaminated_windows`.
- UUID generation is sufficient for V1 reports but is not cryptographic.
- `vis_report_from_json()` and `vis_report_sign()` remain V1 stubs.
- Measurement currently targets Linux x86-64 with MSR access.
- VIS-Mem, VIS Lab, VIS-Infer, and VIS Cell are roadmap items, not V1 features.
- Asserted-but-unprovided report fields are currently emitted as empty strings; a hardened report schema should use explicit `null` or omit unknown optional values.
- CPU behavior classes are based on measured throughput evidence; thermal throttling awareness is not implemented yet, so reports should avoid claiming vendor core labels.

## Validation Commands

Rootless development validation:

```bash
cd vis-jitter
make clean
make
make test
```

SMI/MSR validation requires root and the Linux `msr` module:

```bash
cd vis-jitter
sudo modprobe msr
make test-root
sudo ./vis-jitter --cpu 2 --duration 60 --threshold 100 --output report.json
```

All-core exploratory scan:

```bash
cd vis-jitter
mkdir -p reports
for cpu in $(seq 0 19); do
  sudo ./vis-jitter --cpu "$cpu" --duration 30 --threshold 100 --output "reports/core-${cpu}.json"
done
```

## Next Milestone

V1.2 should focus on report/schema polish and release automation:

- exact overflow max tracking
- machine-readable JSON schema
- `null`/omit semantics for unprovided asserted fields
- stable report examples
- root/non-root CI split
- packaged release artifacts outside the source tree

V1.3 starts VIS Doctor:

- rootless machine inspection
- optional all-core SMI-aware scans using `vis_jitter_run()`
- AI-readable JSON and Markdown diagnostics
- advisory recommendations only; no automatic system mutation

V2 starts VIS Run:

- consume Doctor's `recommended_runtime_policy`
- apply temporary process-level CPU affinity to one workload
- emit terminal and JSON run attestation
- preserve workload exit-code pass-through for scripts/CI
- treat failed actions as evidence with strengthened recommendations
- sample `/proc` during workload execution in best-effort mode
- report observed process/thread affinity escapes before cgroups enforcement exists

Planned hardening:

- thermal/throttle awareness before interpreting low accepted-sample throughput
- richer sensor evidence in VIS Doctor
- cgroups v2 containment for child processes after escape detection
- signed report/attestation support
