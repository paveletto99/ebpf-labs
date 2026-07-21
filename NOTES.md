Use:

```c
bpf_ktime_get_ns()
```

It returns monotonic time in nanoseconds.

So at `sys_enter_openat`, we’d do something like:

```c
__u64 start = bpf_ktime_get_ns();
bpf_map_update_elem(&starts, &tid, &start, BPF_ANY);
```

Then at `sys_exit_openat`, we compute:

```c
latency_ns = bpf_ktime_get_ns() - *start;
```


---


```c
bpf_map_delete_elem(&starts, &tid);
```

That prevents stale entries and keeps the map from filling up.

Now combine the exit-side steps in order:

1. look up the start timestamp by `tid`
2. compute latency
3. submit the event
4. delete the map entry

---
Correct: `bpf_ringbuf_submit()`.

Typical pattern:

```c
struct event *e = bpf_ringbuf_reserve(&events, sizeof(*e), 0);
if (!e)
    return 0;

e->latency_ns = bpf_ktime_get_ns() - *start_time;

bpf_ringbuf_submit(e, 0);
bpf_map_delete_elem(&starts, &tid);
```
Exactly. bpf_ringbuf_reserve() asks for writable space in the ring buffer.

If it returns NULL, there is no available space, so the event must be skipped.
If it succeeds, you fill the reserved memory and call bpf_ringbuf_submit().

>Next question: when reserve fails, should we still delete the tid entry from the starts map?

Correct. Otherwise that completed syscall leaves stale state behind.

```c
struct event *e = bpf_ringbuf_reserve(&events, sizeof(*e), 0);
if (!e) {
    bpf_map_delete_elem(&starts, &tid);
    return 0;
}
```

Now let’s connect this to Go. Suppose the C event contains:

```c
__u64 latency_ns;
```

What Go type should the matching field use: `uint32`, `uint64`, or `int64`?

> [!IMPORTANT]
> The field order in Go must also match the C struct layout.

---

Correct. Filtering inside the eBPF program is usually cheaper because unwanted events never cross into user space.

At syscall exit:

```c
__u64 latency_ns = bpf_ktime_get_ns() - *start_time;

if (latency_ns <= 1_000_000) {
    bpf_map_delete_elem(&starts, &tid);
    return 0;
}
```

Only slower calls continue to `bpf_ringbuf_reserve()` and `bpf_ringbuf_submit()`.

Next question: why must we delete the `starts` entry even when the event is filtered out?

---

## Production tips

Correct. That leaves a stale entry, and enough stale entries can fill the map.

Common causes include a program being detached, missed correlation, or unexpected execution paths. Production tracers often add safeguards such as bounded map sizes, LRU maps, and cleanup logic.

Next question: which map type could automatically evict older entries when capacity is reached—`BPF_MAP_TYPE_HASH` or `BPF_MAP_TYPE_LRU_HASH`?

Correct. `BPF_MAP_TYPE_LRU_HASH` automatically evicts the least recently used entries when the map reaches its maximum size, which helps prevent stale entries from accumulating and filling the map.


Correct: **`lookup_misses`** clearly describes the failure mode.

You could keep a per-CPU counter map and increment it whenever lookup returns `NULL`:

```c
if (!start) {
    /* increment lookup_misses */
    return 0;
}
```

Why use a per-CPU counter instead of one shared global counter?
A per-CPU counter avoids multiple CPUs contending on the same shared value.

Each CPU updates its own slot, so writes are cheaper and safer under high event rates. User space later sums all CPU values to get the total.

So the pattern is:

```text
CPU 0: 12 misses
CPU 1: 7 misses
CPU 2: 3 misses
Total: 22 misses
```

Quick check: what tradeoff do we accept when using per-CPU counters instead of one global counter?

---

## User space aggregation
Correct: **periodically**.

Reading per-CPU maps on every event would add unnecessary overhead. A common pattern is once every few seconds using a Go ticker.

```go
ticker := time.NewTicker(5 * time.Second)
defer ticker.Stop()

for range ticker.C {
	// Read per-CPU lookup_misses values.
	// Sum them.
	// Print or export the total.
}
```

Next question: should this periodic metrics loop block the ring-buffer event reader, or run in a separate goroutine?
Correct: **run in a separate goroutine**.

Blocking the ring-buffer event reader could cause missed events. Running the metrics loop in a separate goroutine ensures that event processing remains responsive.

Correct. Run it in a separate goroutine so metrics aggregation doesn’t delay ring-buffer reads.

```go
go func() {
	ticker := time.NewTicker(5 * time.Second)
	defer ticker.Stop()

	for {
		select {
		case <-ticker.C:
			// Read and sum per-CPU lookup_misses.
		case <-ctx.Done():
			return
		}
	}
}()
```

The ring-buffer reader can continue processing events independently.

Next question: why is `context.Context` useful here instead of leaving the goroutine running forever?

Correct: **it allows graceful shutdown**.

---
## Observability hooks

For observability, the main hook families are:

```text
tracepoints
kprobes/kretprobes
uprobes/uretprobes
fentry/fexit
USDT
```

A kprobe can attach to many kernel functions even when no suitable tracepoint exists.

Use this decision rule:

```text
Prefer tracepoint when it exposes what you need.
Use kprobe when you need an internal function or extra visibility.
```

Example: if no tracepoint exposes a specific filesystem helper, a kprobe may be the only practical hook.

### User-space probes

#### uprobe/uretprobe

```text
uprobe     → user-space function entry
uretprobe  → user-space function return
```

You use them for binaries and shared libraries, such as tracing a Go function, `libc`, or OpenSSL.

> Go function symbol exists in the compiled binary
> for go binary `go tool nm ./opener | grep 'functionName'`

- Why might a function not appear as a usable symbol even though it exists in the source code

Two other common reasons in Go are:

* the compiler **inlined** the function into its callers;
* compiler/linker optimization changed or removed the standalone function body.

For an easier uprobe target during learning, build with optimizations and inlining disabled:

```bash
go build -gcflags="all=-N -l" -o opener .
```

Then inspect symbols again:

```bash
go tool nm ./opener | grep 'main\.'
```

-N → no optimizations
-l → no inlining

#### USDT probes

**named USDT probe** is generally more stable.

A uprobe depends on implementation details such as function names and binary layout. A USDT probe is an intentional observability interface exposed by the application.

```text
uprobe → attach to implementation
USDT   → attach to declared instrumentation point
```
A USDT probe is usually the better long-term contract when the application exposes one.

#### fentry/fexit

Compared with kprobes, fentry/fexit attach through kernel BTF metadata and are generally more efficient and type-aware.

A key advantage over kretprobes is that fexit can often access both the function arguments and return value using BTF type information.


### Kernel metadata

kernel metadata enables that type awareness, it provides type metadata for kernel structs, function parameters, and return values. fentry/fexit and CO-RE use it to understand kernel types more reliably.

**CO-RE** — Compile Once, Run Everywhere.

It uses BTF so the loader can relocate struct field offsets for the target kernel.

```text
BTF  = describes kernel types
CO-RE = adapts your compiled eBPF program to those types
```

**`vmlinux.h`**.

It contains C type definitions generated from the running kernel’s BTF data. A common command is:

```bash
bpftool btf dump file /sys/kernel/btf/vmlinux format c > vmlinux.h
```

A CO-RE program then typically includes:

```c
#include "vmlinux.h"
#include <bpf/bpf_core_read.h>
#include <bpf/bpf_helpers.h>
```

Why should a CO-RE program avoid including ordinary kernel headers alongside `vmlinux.h`?

Partly, but the main issue is **duplicate or conflicting type definitions**.

`vmlinux.h` already contains kernel type definitions derived from BTF. Including ordinary kernel headers too can redefine structs, enums, or typedefs and cause compile errors or mismatched layouts.

Rule:

```text
vmlinux.h → kernel types
libbpf headers → eBPF helpers and CO-RE macros
```


- `BPF_CORE_READ()`.

Example:

```c
struct task_struct *task =
    (struct task_struct *)bpf_get_current_task_btf();

__u32 tgid = BPF_CORE_READ(task, tgid);
```

`BPF_CORE_READ()` follows typed kernel fields using CO-RE relocations. `bpf_map_lookup_elem()` reads values from a BPF map instead.

Correct: `bpf_get_current_task_btf()`.

It returns a BTF-aware pointer to the current kernel `task_struct`, which you can then inspect with `BPF_CORE_READ()`.

```c
struct task_struct *task = bpf_get_current_task_btf();
__u32 tgid = BPF_CORE_READ(task, tgid);
```


* `pid` identifies a specific thread.
* `tgid` identifies the thread group, which corresponds to the process ID users usually see.

For a single-threaded process, `pid == tgid`. For a multithreaded process, every thread has its own `pid`, but they share the same `tgid`.

`pid` means the kernel thread ID, so it is the safer correlation key.

In eBPF, this is commonly extracted as:

```c
__u64 pid_tgid = bpf_get_current_pid_tgid();

__u32 tid  = (__u32)pid_tgid;
__u32 tgid = pid_tgid >> 32;
```

Use `tid` to pair entry and exit. Use `tgid` to group events by process. Using `tgid` could cause concurrent threads in the same process to overwrite each other’s start timestamp.

```text
tid  → correlate one thread’s entry and exit
tgid → group or display the whole process
```

- for a metric like “slow opens per process,” would you group by `tid` or `tgid`?
Correct: **`tgid`**.

That groups all threads belonging to the same process into one metric.

```text
tid  → event correlation
tgid → per-process aggregation
```

- for “slow opens per thread,” which key would you use?
Correct: **`tid`**.

You now have the key distinction:

```text
Entry/exit correlation → tid
Per-thread metrics      → tid
Per-process metrics     → tgid
User-visible process ID → tgid
```

### Fallbacks
bpf_core_field_exists() lets the loaded program adapt to the target kernel’s BTF. If a field is missing, use a **fallback path**.

```c
if (bpf_core_field_exists(task->some_field)) {
    value = BPF_CORE_READ(task, some_field);
} else {
    value = default_value;
}
```

That preserves compatibility across kernels.

**a default value can be correct**, but it depends on meaning.

Use a default when “unknown” can be represented safely, such as:

```c
value = 0;
```

But if `0` is also a valid real value, add a validity flag:

```c
struct event {
    __u64 value;
    __u8 value_valid;
};
```

Then user space can distinguish:

```text
value_valid = 1 → real value
value_valid = 0 → field unavailable
```

 A validity flag prevents **ambiguity**: without it, Go cannot tell whether `0` is the real kernel value or merely the fallback because the field was unavailable.

```text
value = 0, valid = 1 → genuine zero
value = 0, valid = 0 → unavailable
```

**`uint8`** is safer for matching a C `__u8` field exactly.

```go
type event struct {
	Value      uint64
	ValueValid uint8
}
```

Then in Go:

```go
if e.ValueValid != 0 {
	// value is available
}
```

> [!IMPORTANT] must match between the C and Go structs? `Types` and `field` **order** must match exactly. Otherwise, Go will misinterpret the binary layout of the event data. Field sizes, alignment, padding.

👀 Put the largest field first:

```c
struct event {
    __u64 value;
    __u8 valid;
};
```

This removes the padding **between** fields, though the struct may still have trailing padding for alignment.

> 🫡 **largest fields first** `__u64 → __u32 → __u16 → __u8 → arrays`


**whole struct’s size is rounded up to satisfy its strictest alignment requirement**.

Example:

```c
struct event {
    __u64 value; // 8 bytes
    __u8 valid;  // 1 byte
};
```

The fields use 9 bytes, but the struct may become **16 bytes** so arrays of `struct event` keep each `value` aligned to 8 bytes.

* Tool or Go function could you use to verify the actual size of the matching Go struct?

Use `unsafe.Sizeof()`:

```go
fmt.Println(unsafe.Sizeof(event{}))
```

For field offsets, use `unsafe.Offsetof()`:

```go
var e event

fmt.Println(unsafe.Offsetof(e.Value))
fmt.Println(unsafe.Offsetof(e.Valid))
```

These help verify that the Go layout matches the C struct.

Inspect alignment and padding.

A reliable check is:

```go
fmt.Println("size:", unsafe.Sizeof(event{}))
fmt.Println("Value offset:", unsafe.Offsetof(event{}.Value))
fmt.Println("Valid offset:", unsafe.Offsetof(event{}.Valid))
```

Then compare those offsets with the C layout.

**Unit Test**: A useful unit test is verifying the decoded event structure’s size and field offsets.

For example, test that:

```text
LatencyNs starts at the expected offset
Comm has exactly 16 bytes
Filename has exactly 256 bytes
The total event size matches the generated C layout
```

You can also test pure Go helpers such as `cString()` and latency conversion without loading eBPF.
