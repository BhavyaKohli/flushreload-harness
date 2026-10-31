# Flush+Reload accuracy vs VCPU load

## Prerequisites (one-time)

The experiment requires at least two vCPUs. The loader is pinned to CPU 0 and
calibration/`newspy` are pinned to CPU 1, so cache noise from the loader is
concurrent with the attack on the other vCPU.
`calibration.c`, `newspy.cpp`, and `loadgen.c` must all be present in
`~/Flush-Reload` on the VM.

```bash
scp.exe vm/loadgen.c flrl:Flush-Reload/loadgen.c
scp.exe vm/newspy.cpp flrl:Flush-Reload/newspy.cpp
scp.exe vm/calibration.c flrl:Flush-Reload/calibration.c
ssh.exe flrl "cd ~/Flush-Reload && gcc -O2 -o loadgen loadgen.c"
ssh.exe flrl "cd ~/Flush-Reload && gcc -O2 -o calibration calibration.c"
ssh.exe flrl "cd ~/Flush-Reload && g++ -O2 -o newspy newspy.cpp \
    -I/usr/local/include/openssl \
    -L\$HOME/opt/openssl-1.1.0f/lib \
    -Wl,-rpath,\$HOME/opt/openssl-1.1.0f/lib -lcrypto"
```

Note: do **not** pass `-fsanitize=address` — it distorts `rdtsc` timing and
has previously caused `mmap` failures.

## Commands used for the recorded run

These were run from the repository root on WSL. `ssh.exe` and `scp.exe` are
the Windows OpenSSH binaries; `flrl` is the VM SSH alias.

```bash
scp.exe vm/loadgen.c vm/newspy.cpp vm/calibration.c flrl:Flush-Reload/
ssh.exe flrl 'cd ~/Flush-Reload && gcc -O2 -o loadgen loadgen.c && gcc -O2 -o calibration calibration.c && g++ -O2 -o newspy newspy.cpp -I/usr/local/include/openssl -L$HOME/opt/openssl-1.1.0f/lib -Wl,-rpath,$HOME/opt/openssl-1.1.0f/lib -lcrypto && echo BUILD_OK'
python3 run_experiments.py both --mode mem
python3 run_experiments.py both --mode evict --levels 0 10 20 30 40 50 60 70 80 90 100
```

## Run

```bash
python run_experiments.py both --mode mem
```

Or one experiment at a time:

```bash
python run_experiments.py exp1 --levels 0 30 60 90 --reps 5
python run_experiments.py exp2 --mode busy
```

Outputs go to `results/`:

| File | Contents |
|---|---|
| `<stamp>_exp1.csv` | Retained exp1 results for that run |
| `<stamp>_exp2.csv` | Retained exp2 results for that run |
| `exp1.csv`, `exp2.csv` | Latest results for each experiment |

## Load model

`loadgen {mem|busy|target|evict} DUTY` runs a 20 ms duty cycle: `DUTY`% work,
rest `nanosleep`. `mem` streams writes through a 256 MB buffer (larger than the
72 MB LLC), `busy` runs a short ALU loop, `target` touches the four OpenSSL
table lines newspy watches, and `evict` flushes those same four lines with
`clflushopt`. Actual load is measured from
`/proc/<pid>/stat` before newspy runs, over an approximately 3 s window using
actual elapsed VM uptime. `load_pct` is the utilization of the loader's pinned
vCPU, so it remains on a 0–100% scale.

The loader remains running during calibration and `newspy`, and is terminated
only after `newspy` exits or the remote script is interrupted.
Each `calibration` run is capped at 60 s and each `newspy` run at 300 s; a
timeout aborts the run with a non-zero exit.

## Experiment semantics

- **exp1** — threshold `T0` from 10 zero-load calibrations, then used at
  every level. Shows how a threshold that is correct at 0 % load degrades
  under load.
- **exp2** — 10 calibration runs at each level, whose mean feeds
  newspy. Shows whether recalibrating recovers accuracy.

## Recorded results

### Uniform load (`mem`)

The full `both --mode mem` run produced
`results/20261031_165807_exp1.csv` and `results/20261031_165807_exp2.csv`.

- exp1: 30/30 runs recovered all 16 bytes with the zero-load threshold
  `T0 = 266`, over measured load 0.7%–86.6%.
- exp2: 29/30 runs recovered all 16 bytes; one run at the 40% level
  (38.9% measured load) recovered 2/16 bytes.

Uniform LLC traffic did not produce a monotonic accuracy drop. It jitters the
calibration threshold and causes rare stochastic failures, but Flush+Reload
stays robust here because each encryption starts with `clflush`, resetting
the probe line's cache state.

### Targeted eviction (`evict`)

The full `both --mode evict --levels 0 10 20 30 40 50 60 70 80 90 100` run
produced `results/20261031_200557_exp1.csv` and
`results/20261031_200557_exp2.csv`.

- exp1: 30/33 runs recovered all 16 bytes up to 90% load; one 50% rep
  recovered 7/16 bytes, and all three 100% reps recovered 0/16 bytes.
- exp2: 31/33 runs recovered all 16 bytes up to 90% load; one 90% rep
  recovered 3/16 bytes, and all three 100% reps recovered 0/16 bytes.

Duty-cycled targeted eviction only collapses Flush+Reload when the evictor
runs continuously (100% duty). Below 100%, the off windows still provide
enough clean reload samples for the attack to recover.
