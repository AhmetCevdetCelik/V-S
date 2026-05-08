# VIS Doctor Output

VIS Doctor is the diagnostic layer above `vis-jitter`. It inspects the machine,
optionally runs SMI-aware all-core scans, and emits evidence in two forms:

- terminal summary for humans
- JSON and Markdown context designed to be pasted into AI assistants

## Commands

Rootless inspection:

```bash
./vis-doctor --inspect
```

SMI-aware scan:

```bash
sudo ./vis-doctor --scan --duration 30 --threshold 100 --output doctor.json --llm doctor.md
```

## Example Terminal Shape

```text
VIS Doctor
Status: CLEAN WITH NOTES

Machine: ahmet-Victus-by-HP-Gaming-Laptop-15-fa1xxx | Linux ...
CPUs: 20 online | SMT: yes | isolated: 2 | nohz_full: 2
Primary candidate CPUs: 0,2,4,6,8,10
All clean higher-throughput CPUs: 0,1,2,3,4,5,6,7,8,9,10,11

Findings:
  warning smi: Some CPUs had SMI-contaminated measurement windows.
  info cpu_topology: Some CPUs show lower accepted-sample throughput.

Recommendations:
  safe: Validate candidate CPUs with a longer scan.
  safe: Prefer sibling-aware primary candidate CPUs: 0,2,4,6,8,10
  safe: Use the full clean logical CPU list as secondary evidence: 0,1,2,3,4,5,6,7
  advanced: Account for SMT sibling noise before pinning critical work.
```

## AI Context Design

The Markdown report starts with:

```text
# VIS Doctor AI Context
```

It includes:

- machine facts
- findings
- sibling-aware primary candidate CPUs
- all clean higher-throughput logical CPUs
- a machine-readable `recommended_runtime_policy` for `vis-run`
- recommendations with:
  - why the finding matters
  - safe suggestion
  - advanced suggestion
  - advanced risk
  - validation command
- compact per-CPU scan evidence
- a note that VIS Doctor V1.3 is advisory only and does not apply system changes

The intent is that a user can paste `doctor.md` into an AI assistant and get
grounded advice without VIS Doctor applying risky system changes by itself.

`recommended_runtime_policy` is the bridge from diagnosis to action. It should
be parsed by VIS runtime tools instead of scraping natural-language
recommendations.
