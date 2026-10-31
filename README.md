# Flush+Reload accuracy vs VCPU load

## Prerequisites (one-time)

The VM needs at least two vCPUs. The harness pins `loadgen` to CPU 0 and
calibration/`newspy` to CPU 1 so cache noise runs concurrently with the attack.

    scp.exe vm/loadgen.c flrl:Flush-Reload/loadgen.c
    ssh.exe flrl "cd ~/Flush-Reload && gcc -O2 -o loadgen loadgen.c"
    ssh.exe flrl "cd ~/Flush-Reload && g++ -O2 -o newspy newspy.cpp \
        -I/usr/local/include/openssl \
        -L\$HOME/opt/openssl-1.1.0f/lib \
        -Wl,-rpath,\$HOME/opt/openssl-1.1.0f/lib -lcrypto"

Note: do **not** pass `-fsanitize=address` — it distorts `rdtsc` timing and
has previously caused `mmap` failures.

## Run

    python run_experiments.py both --mode mem

For targeted defensive noise against the monitored OpenSSL table lines:

    python run_experiments.py both --mode target
    python run_experiments.py both --mode evict

For victim-integrated cache-state equalization:

    python run_experiments.py both --mode mem --victim-defense

Or one experiment at a time:

    python run_experiments.py exp1 --levels 0 30 60 90 --reps 5
    python run_experiments.py exp2 --mode busy

Outputs go to `results/`:

| File | Contents |
|---|---|
| `<stamp>_exp1.csv` | Retained exp1 results for that run |
| `<stamp>_exp2.csv` | Retained exp2 results for that run |
| `exp1.csv`, `exp2.csv` | Latest results for each experiment |
| `raw.jsonl` | every data point as JSON, append-only |
| `meta_<stamp>.json` | config snapshot per run |

## Load model

`loadgen {mem|busy|target|evict} DUTY` runs a 20 ms duty cycle: `DUTY`% work, rest
`nanosleep`. `mem` writes randomly selected cache lines in a 64 MB buffer,
`busy` runs a short ALU loop, and `target` repeatedly accesses the four shared
OpenSSL table lines monitored by `newspy`. `evict` repeatedly flushes those
lines to invalidate victim-created cache hits. Actual load is measured from
`/proc/<pid>/stat` before newspy runs, over an approximately 3 s window using
actual elapsed VM uptime. `load_pct` is the utilization of the loader's pinned
vCPU, so it remains on a 0–100% scale.

`--victim-defense` touches every cache line in the four AES lookup tables after
each encryption and before returning to the reload step. This equalizes the
observable cache state instead of relying on independent noise that can be
averaged away.

## Experiment semantics

- **exp1** — threshold `T0` from `CALIB_RUNS` zero-load calibrations, then used at
  every level. Shows how a threshold that is correct at 0 % load degrades
  under load.
- **exp2** — `CALIB_RUNS` calibration runs at each level, whose mean feeds
  newspy. Shows whether recalibrating recovers accuracy.
