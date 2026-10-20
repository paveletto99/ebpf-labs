# Speaker guide: eBPF: Programming the Linux Kernel

A 30-minute Linux Day slot: **25 minutes of talk (including a 5-minute live demo) and 5 minutes of questions.**
Audience: Linux enthusiasts and developers with no previous eBPF knowledge.
Slide source: [../slides.md](../slides.md). Presenter notes with timings and references are also inside the slides.

Placeholders to fill before the talk: speaker name on slide 1. All examples are **controlled examples**; do not describe them as real incidents unless you have a real one to tell.

## Timing

| # | Slide | Minutes | Clock | Section |
| --- | --- | ---: | --- | --- |
| 1 | Title | 0.5 | 0:00–0:30 | Problem |
| 2 | A controlled example: the app that never starts | 1.5 | 0:30–2:00 | Problem |
| 3 | Two worlds: user space and kernel space | 1 | 2:00–3:00 | What eBPF is |
| 4 | What is eBPF? (origins in packet filters) | 2 | 3:00–5:00 | What eBPF is |
| 5 | Load once, run on every event (architecture diagram) | 2.5 | 5:00–7:30 | How it works |
| 6 | Before it runs: verifier and JIT | 1.5 | 7:30–9:00 | How it works |
| 7 | Maps: memory and a mailbox | 1 | 9:00–10:00 | How it works |
| 8 | Live demo: find the missing file | 5 | 10:00–15:00 | Demo |
| 9 | Observability | 1.5 | 15:00–16:30 | Applications |
| 10 | Networking | 1.5 | 16:30–18:00 | Applications |
| 11 | Security | 1 | 18:00–19:00 | Applications |
| 12 | Observe, or change behavior? | 1 | 19:00–20:00 | Applications |
| 13 | Engineering realities | 3 | 20:00–23:00 | Constraints |
| 14 | Getting started and resources | 1 | 23:00–24:00 | Wrap-up |
| 15 | Takeaways | 1 | 24:00–25:00 | Wrap-up |
| | **Talk total** | **25** | | |
| | Questions | 5 | 25:00–30:00 | Q&A |
| 16 | Backup (illustrative output, not in the timing) | 0 | | Backup |

Section check: 0–2 problem (2.0), 2–5 what it is (3.0), 5–10 how it works (5.0), 10–15 demo (5.0),
15–20 applications (5.0), 20–23 constraints (3.0), 23–25 getting started and takeaways (2.0). Sum: 25.

Checkpoints: slide 5 starts at 5:00, the demo starts at 10:00, slide 13 starts at 20:00. If you are more than a minute behind at 10:00, shorten slide 8's explanation, not the demo.

## Speaking notes

### 1. Title (0:30)
Welcome. Promise: "In 25 minutes you will know what eBPF is, how it works, and you will see it solve a small problem live."

### 2. The stuck app (1:30)
Say it is a controlled example prepared for this talk. Ask: "How would you find what a silent program is waiting for?" Collect two or three answers (code, logs, debugger, strace). Land the point: every file open goes through the kernel, so we can ask the kernel.

### 3. User space and kernel space (1:00)
Define kernel space and user space in one sentence each. A system call is the doorway. Name `openat` as the system call used to open a file. Say: the kernel sees everything programs do, but it was hard to extend.

### 4. What is eBPF? (2:00)
One sentence: "Small programs that run inside the kernel when events happen." Origin: BPF began as a packet filter (1990s, `tcpdump`). Linux 3.18 (2014) added extended BPF and the `bpf()` system call. Today "eBPF" is a standalone name. No kernel rebuild and no reboot.
Avoid "eBPF can do anything in the kernel".

### 5. Architecture (2:30)
Walk the diagram in two passes.
Pass 1, **load time** (blue, solid, once): tool → `bpf()` → verifier → JIT → attach to a hook.
Pass 2, **run time** (orange, dashed, every event): event fires → program runs → reads and writes a map → the tool reads results.
Key sentence: "Loading is not running. A loaded program waits for its event."

### 6. Verifier and JIT (1:30)
Verifier: the kernel checks the program before it can run: memory safety, termination, allowed helpers. Rejected programs never run. Do not say the verifier guarantees no bugs. JIT: translates bytecode to machine code where supported. No instruction-set or verifier-internals discussion.

### 7. Maps (1:00)
A program runs once per event and ends. A map is key-value storage that survives between events and can be read from user space. Tie it to the demo: the map remembers the filename between syscall entry and exit.
Transition into the demo: "Let's use all of this. One app, two terminals."

### 8. Live demo (5:00): see the script below

### 9. Observability (1:30)
Transition out of the demo: "What we just did is the smallest version of a very large idea." Same pattern: hook, program, map, reader. Mention in-kernel aggregation (counts and histograms in maps, only summaries leave the kernel). No performance numbers.

### 10. Networking (1:30)
Packet filtering is where BPF started. XDP runs at the network driver and returns pass, drop, or redirect. Examples: Cilium, Katran. Support depends on drivers and kernel version.

### 11. Security (1:00)
Watching: process starts, file access, network connections (Falco, Tetragon). Enforcing: only at hooks designed for it, such as BPF LSM. `seccomp` also uses BPF filters. Warn: bad policy can block good software.

### 12. Observe or change? (1:00)
Observe is the default for tracing. Behavior changes only where a hook allows it, and the hook decides what is visible and what can be returned. Say "within what the hook permits".

### 13. Engineering realities (3:00)
About 45 seconds each: privileges, limits, portability, overhead. Say plainly: overhead is not zero and depends on how often the hook fires and what the program does; measure. Not every program runs unchanged on every kernel. Mention CO-RE and BTF as the mitigation, not a cure.

### 14. Getting started (1:00)
Start with bpftrace one-liners, then libbpf or a language library. Point to ebpf.io and docs.kernel.org/bpf. The demo source is in this repository.

### 15. Takeaways (1:00), then questions
Read the four takeaways. Leave the slide up for questions.

## Five-minute demo script (10:00–15:00)

Exact commands and expected output: [../demo/README.md](../demo/README.md). Run the preflight and `sudo -v` before the talk.

| Time | Do | Say |
| --- | --- | --- |
| 10:00 | Slide 8 is on screen. | "bpftrace turns this small script into an eBPF program. Two probes: when `openat` is entered, remember the filename in a map keyed by thread. When it returns, if the result is -2, which is `ENOENT`, print the name." |
| 11:00 | Switch to the terminals. Terminal A: `cd …/docs/slides/demo`, `rm -f -- ./config.json`, `python3 app.py`. | "Here is the stuck app. It prints its PID and says it is starting. No error." Point at the PID. |
| 11:45 | Terminal B: `sudo bpftrace missing-files.bt <PID>`. | "I need root to load the program. It is checked by the verifier and attached to two tracepoints." |
| 12:15 | Wait for two or three lines. | "There it is: `python3` keeps getting ENOENT for `…/demo/config.json`, once per second. The app never told us." |
| 13:15 | Ctrl+C in Terminal B. | "I stop the tracer. The program is detached and the map is cleaned up." |
| 13:30 | Terminal B: `echo '{"message": "Hello, Linux Day!", "mode": "demo"}' > config.json`. | "Now I create the file it was waiting for." |
| 13:45 | Terminal A shows `Ready!`. | "Fixed. We observed a live process without changing or restarting it." |
| 14:15 | Back to slides. | Scope sentence: "This traced `openat` only, not every way to open a file. Next: where this idea goes." |
| 15:00 | Advance to slide 9. | |

Slack: about 45 seconds (14:15–15:00) are left for the scope sentence and overruns. If you are late, shorten the 12:15–13:15 stretch.

## Shorter fallback if the live demo fails

Aim: 2 minutes, then continue on time. Decide within 30 seconds on stage.

1. Say: "The live tracer is not cooperating. Let me show you what it would show."
2. Show the recording or text capture you prepared on the presentation laptop and say that it is a recording. If you have none, show the backup slide (slide 16), which is **illustrative output, not captured**; say so.
3. In Terminal A (app only, no eBPF needed) create `config.json` to show `Ready!`.
4. Read the one-line explanation: entry remembers the name, exit prints it if the result is `ENOENT`.
5. Move on to slide 9. Use the saved minutes for questions.

## Likely questions

**Could `strace` do this?**
Yes. `strace -f -e trace=openat -p <PID>` (or starting the app under strace) would also show this example, and it is a good tool for it. This demo is a small example that introduces eBPF mechanisms: programs attached to hooks, maps, and aggregation in the kernel. Those extend to broader tracing, such as many processes at once or counting and summarizing events in the kernel before sending results to user space. `strace` and eBPF overheads differ by workload; measure on your own system rather than assuming either is cheaper.

**How does eBPF differ from a kernel module?**
A kernel module is native code loaded into the kernel with full kernel privileges. A bug in a module can crash or corrupt the system. An eBPF program is bytecode that the verifier checks before it is attached, runs only at defined hooks, and can only call approved helper functions. Trade-off: eBPF is more constrained than a module. Modules can do things eBPF cannot. Both need privileges to load.

**What does the verifier check?**
At a high level: that the loader has the required privileges, that the program terminates, that memory accesses are safe, and that only allowed helper functions and arguments are used. It rejects programs it cannot prove safe. It does not prove the program does what you intended, and no verifier removes the possibility of bugs in the kernel itself.

**What overhead does tracing add?**
It is not zero. Each time the hook fires your program runs, so the cost depends on how frequent the event is and how much work the program does. A tracepoint on a rare event costs little; one on every packet or syscall on a busy system can matter. Aggregating in the kernel can reduce data sent to user space. Measure with your workload. I have no benchmark to quote from this demo.

**Which privileges and kernel features are needed?**
Loading usually needs root or capabilities such as `CAP_BPF` (Linux 5.8+), often with `CAP_PERFMON` for tracing or `CAP_NET_ADMIN` for networking. Unprivileged eBPF may be disabled (`kernel.unprivileged_bpf_disabled`). Needed kernel features include `CONFIG_BPF_SYSCALL`, the specific hook type (for example syscall tracepoints need `CONFIG_FTRACE_SYSCALLS`), and for CO-RE portability BTF (`CONFIG_DEBUG_INFO_BTF`). Exact requirements depend on the program type and kernel version, so check the target kernel.

**Other short answers**
- *Is it only for Linux?* This talk covers Linux. eBPF implementations for other systems exist but are outside this talk.
- *Can I use it without root?* Sometimes, if your system allows unprivileged eBPF or grants capabilities; do not assume it.
- *Can I break my system with bpftrace?* Tracing is designed to be safe by verification, but run experiments on a test machine. Some hooks can affect behavior (networking, security).
- *Where do I start?* bpftrace, ebpf.io/get-started, and docs.kernel.org/bpf.

## References (official first)

- What is eBPF: https://ebpf.io/what-is-ebpf/
- Get started: https://ebpf.io/get-started/
- Applications landscape: https://ebpf.io/applications/
- Kernel BPF documentation: https://docs.kernel.org/bpf/
- Classic BPF vs eBPF: https://docs.kernel.org/bpf/classic_vs_extended.html
- Verifier: https://docs.kernel.org/bpf/verifier.html
- Maps: https://docs.kernel.org/bpf/maps.html
- BPF design Q&A (stack size and more): https://docs.kernel.org/bpf/bpf_design_QA.html
- BTF: https://docs.kernel.org/bpf/btf.html
- libbpf and CO-RE: https://docs.kernel.org/bpf/libbpf/index.html
- BPF LSM: https://docs.kernel.org/bpf/prog_lsm.html
- Tracepoints: https://docs.kernel.org/trace/tracepoints.html
- bpf(2): https://man7.org/linux/man-pages/man2/bpf.2.html
- bpf-helpers(7): https://man7.org/linux/man-pages/man7/bpf-helpers.7.html
- capabilities(7): https://man7.org/linux/man-pages/man7/capabilities.7.html
- seccomp(2): https://man7.org/linux/man-pages/man2/seccomp.2.html
- openat(2): https://man7.org/linux/man-pages/man2/openat.2.html
- bpftrace: https://github.com/bpftrace/bpftrace and its language reference
- Steven McCanne and Van Jacobson, "The BSD Packet Filter" (USENIX Winter 1993): https://www.tcpdump.org/papers/bpf-usenix93.pdf
- Liz Rice, *Learning eBPF* (O'Reilly, 2023); Brendan Gregg, *BPF Performance Tools* (Addison-Wesley, 2019)
