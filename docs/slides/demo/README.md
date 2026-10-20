# Live demo runbook: the app that never starts

A controlled example built for the talk (not a real incident). `app.py` looks stuck at startup
because `config.json` does not exist. `missing-files.bt` (bpftrace) reveals the failing
`openat` calls. Creating `config.json` lets the app finish.

| File | Purpose |
| --- | --- |
| [app.py](app.py) | Python app. Prints `Starting…` and its PID, retries reading `config.json` once per second, then prints `Ready!`. Runs as an ordinary user. |
| [missing-files.bt](missing-files.bt) | bpftrace script. Takes the PID as `$1` and prints each `openat` that fails with `ENOENT`. |

**Deliberate scope:** the tracer attaches to the `openat` system call only
(`sys_enter_openat` and `sys_exit_openat`). It does not see every way a program can open a file
(for example `open`, `openat2`, or `io_uring`). Python's `open()` uses `openat` on modern Linux.
That is enough for this demo.

**Tested target:** bpftrace v0.23.x (uses `args.field` and the two-argument `delete()`).
On bpftrace older than 0.21, replace `args.` with `args->`.

All commands below use the absolute path of this repository on the presentation laptop:
`/home/pobo/workspace/ebpf-labs`. Adjust it if the repository lives elsewhere.

## 1. Before going on stage

Run these in a terminal, on the laptop you will present from.

```bash
# Authenticate sudo now, so no password prompt appears on stage.
sudo -v
```

Re-run `sudo -v` shortly before the talk. The sudo timestamp expires (often after 5–15 minutes).
Check your system's timeout with `sudo -V | grep -i timeout`.

### Preflight check

```bash
cd /home/pobo/workspace/ebpf-labs/docs/slides/demo

python3 --version                       # any Python 3.8+ works
bpftrace --version                      # tested with v0.23.2
id -u                                   # ordinary user: not 0
sudo -n true && echo "sudo OK"          # fails if sudo is not pre-authenticated

# Both tracepoints must exist, with these fields:
sudo bpftrace -lv 'tracepoint:syscalls:sys_enter_openat'
sudo bpftrace -lv 'tracepoint:syscalls:sys_exit_openat'

# Check that the script compiles, loads, and attaches (no events needed):
sudo bpftrace --dry-run missing-files.bt 1
```

Expected:

- `sys_enter_openat` lists the fields `int dfd`, `const char * filename`, `int flags`, `umode_t mode`.
- `sys_exit_openat` lists the field `long ret`.
- `--dry-run` exits without error. A syntax error or a missing tracepoint appears here.

If a field name differs on your kernel, fix the script before the talk (see Troubleshooting).

### Rehearse the whole demo once

Follow section 2 end to end, then section 4 to reset.

## 2. The demo (about 4 minutes live)

Two terminals, side by side. Use a large font.

### Reset first

```bash
# Terminal A (or any terminal)
cd /home/pobo/workspace/ebpf-labs/docs/slides/demo
rm -f -- ./config.json
ls                                      # config.json must not be listed
```

### Terminal A: start the application

```bash
cd /home/pobo/workspace/ebpf-labs/docs/slides/demo
python3 app.py
```

Expected output (the PID will differ; the counter keeps growing):

```text
Starting…
PID: 12345
Still starting… (attempt 1)
Still starting… (attempt 2)
```

Say: "It is stuck. There is no error and no filename. What is it waiting for?"
Note the PID printed after `PID:`. Leave this terminal running.

### Terminal B: trace it

Replace `12345` with the PID from Terminal A.

```bash
cd /home/pobo/workspace/ebpf-labs/docs/slides/demo
sudo bpftrace missing-files.bt 12345
```

Expected output: one header line, then one line per second. The shape is:

```text
Tracing failed openat() calls for PID 12345... Ctrl+C to stop.
python3    ENOENT  /home/pobo/workspace/ebpf-labs/docs/slides/demo/config.json
python3    ENOENT  /home/pobo/workspace/ebpf-labs/docs/slides/demo/config.json
```

*(Illustrative: this documents the expected format. It is not a captured run. See
“Capture fallback output” below to record the real output.)*

What it means:

- `python3`: the process name (`comm`) that made the call.
- `ENOENT`: the kernel returned `-2`, "No such file or directory".
- The path: the exact file the app keeps asking for. The app never logged it.

### Stop the tracer, then fix the problem

In Terminal B press **Ctrl+C**. The tracer stops and cleans up its map.
Then, in the same terminal:

```bash
cd /home/pobo/workspace/ebpf-labs/docs/slides/demo
echo '{"message": "Hello, Linux Day!", "mode": "demo"}' > config.json
```

Terminal A should show, within about one second:

```text
Ready!
{
  "message": "Hello, Linux Day!",
  "mode": "demo"
}
```

The app then exits by itself. The success path was run during preparation; see the
validation status in [../README.md](../README.md).

## 3. If the live demo fails

1. **Decide quickly.** If the tracer does not show output within about 30 seconds, stop debugging on stage.
2. **Show the app path only.** Terminal A still works without eBPF: create `config.json` and show `Ready!`.
3. **Show a recording or captured output.** Use the recording or text capture you prepared (below).
   Say clearly that it is a recording or capture.
4. **Last resort:** use the backup slide at the end of the deck. Its output is *illustrative*, not captured, and the slide says so.
5. Continue with the speaker guide's [short fallback](../docs/speaker-guide.md#shorter-fallback-if-the-live-demo-fails).

### Capture fallback output (do this once on the presentation laptop)

Nothing in this repository claims a recording exists. Create one yourself:

```bash
# Terminal A
cd /home/pobo/workspace/ebpf-labs/docs/slides/demo
rm -f -- ./config.json
python3 app.py

# Terminal B (replace 12345)
cd /home/pobo/workspace/ebpf-labs/docs/slides/demo
sudo bpftrace missing-files.bt 12345 | tee /tmp/ebpf-demo-capture.txt
# Ctrl+C after a few lines, then:
echo '{"message": "Hello, Linux Day!", "mode": "demo"}' > config.json
```

Also record the screen (for example with OBS) as a video, and keep both files on the laptop.
After capturing real output, you may replace the backup slide's block in `slides.md`.

## 4. Reset

The only file the demo generates is `config.json` in this directory. Reset with:

```bash
cd /home/pobo/workspace/ebpf-labs/docs/slides/demo
rm -f -- ./config.json
```

If a previous run is still going, stop it first (Ctrl+C in its terminal). Find stray runs with
`pgrep -af 'python3 app.py'`.

## 5. Troubleshooting

| Symptom | Likely cause | What to do |
| --- | --- | --- |
| `ERROR: bpftrace currently only supports running as the root user.` | Ran `bpftrace` without `sudo`. | Use `sudo bpftrace missing-files.bt PID`. |
| `sudo` asks for a password on stage | Sudo timestamp expired. | Type it, or run `sudo -v` before the talk. Do not leave a passwordless sudo rule behind. |
| `Operation not permitted` / `Permission denied` as root | Kernel lockdown (for example Secure Boot), a container without `CAP_BPF`/`CAP_SYS_ADMIN`, or a restricted sandbox. | Run on the host, not in a restricted container. Check `dmesg` and `cat /sys/kernel/security/lockdown`. |
| `No probe matches` / `ERROR: ... sys_enter_openat` | Tracepoints missing: `tracefs` not mounted, or the kernel lacks syscall tracepoints (`CONFIG_FTRACE_SYSCALLS`). | `sudo bpftrace -l 'tracepoint:syscalls:*openat*'`. Mount tracefs: `sudo mount -t tracefs nodev /sys/kernel/tracing`. |
| Syntax error mentioning `args.` | bpftrace older than 0.21. | Use `args->filename` and `args->ret`, or upgrade bpftrace. |
| Tracer prints the header but no `ENOENT` lines | Wrong PID, the app already exited, or `config.json` already exists. | In another terminal: `ps -p 12345 -o pid,comm,args`. Re-check the PID in Terminal A. Reset (section 4) and restart the app. |
| Usage message and immediate exit | The PID argument is missing (it defaults to 0). | Pass the PID: `sudo bpftrace missing-files.bt 12345`. |
| Tracer shows lines for other files | Another thread or library in the app opened files. | Only `ENOENT` lines are printed. Point to the repeated `config.json` line. |
| `Ready!` never appears after creating the file | File created in the wrong directory, or invalid JSON. | The app reads `config.json` next to `app.py`. Create it in `.../docs/slides/demo/` with valid JSON. |

Useful checks:

```bash
pgrep -af 'python3 app.py'                       # find the app and its PID
ls -l /home/pobo/workspace/ebpf-labs/docs/slides/demo/config.json
cat /proc/sys/kernel/unprivileged_bpf_disabled   # information only
```

## 6. How the script works (for questions)

- `sys_enter_openat` fires when the app calls `openat`. The filename is available then, so the probe saves it in a map keyed by thread ID (`tid`).
- `sys_exit_openat` fires when the call returns. The return value is available then, but the filename is not. If the value is `-2` (`ENOENT`), the probe prints the saved name. It always deletes that thread's entry afterwards.
- Both probes filter on `pid == $1`. In bpftrace, `pid` is the process ID (the kernel's thread-group ID).
- `BEGIN` prints a usage message if no PID was given. `END` clears the map when you press Ctrl+C.
