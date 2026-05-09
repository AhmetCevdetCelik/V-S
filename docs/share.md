# Share VIS

Use this text when asking someone to try VIS.

## Short version

I am testing VIS, an open-source Linux/x86_64 prototype for measuring CPU
jitter, SMI contamination, runtime drift, and workload placement evidence.

It does not change persistent system settings. The community test runs a CPU
diagnosis, produces `doctor.md`, and compares basic runtime profiles.

Repo:
https://github.com/AhmetCevdetCelik/V-S

Community test guide:
https://github.com/AhmetCevdetCelik/V-S/blob/main/docs/community-testing.md

## Slightly longer version

VIS is a deterministic performance project. The idea is that critical software
should not run on hardware blindly; it should run with a measured, explainable,
repeatable execution profile.

The current prototype includes:

- `vis-jitter`: CPU jitter and SMI-contaminated window measurement
- `vis-doctor`: machine diagnosis and AI-readable reports
- `vis-run`: workload-scoped CPU policy attestation
- `vis-compare`: profile comparison and optional workload metric capture

If you have a Linux/x86_64 machine, reports from different hardware would be
very useful. Please remove private hostnames, usernames, paths, serial numbers,
or other identifiers before posting results.

Repo:
https://github.com/AhmetCevdetCelik/V-S
