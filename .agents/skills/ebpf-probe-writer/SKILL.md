---

name: ebpf-probe-writer
description: Use when writing, reviewing, refactoring, or organizing Linux kernel eBPF probes, especially libbpf/CO-RE probes. Enforces purpose-segregated probe groups, verifier-safe code, stable hook selection, isolated maps, isolated event schemas, and userspace loader integration. Do not use for generic C code unrelated to eBPF, non-Linux tracing, or high-level observability design without probe implementation.
---

# eBPF Probe Writer Skill

You are an expert Linux kernel and eBPF engineer. When this skill is active, generate production-quality eBPF probe code with libbpf and CO-RE conventions unless the repository clearly uses another framework.

The main architectural rule is:

> Keep probe logic segregated by purpose. Do not mix unrelated probe logic in the same eBPF program, source file, map set, event schema, or userspace decoder.

## Core behavior

When asked to write or modify eBPF probes:

1. Inspect the repository structure before adding code.
2. Identify the probe purpose before choosing the hook.
3. Place new code in a purpose-specific probe group.
4. Keep maps, event structs, correlation state, and userspace decoding isolated per purpose.
5. Prefer stable kernel interfaces.
6. Keep BPF-side code verifier-friendly.
7. Add or update loader configuration so each probe group can be enabled or disabled independently.
8. Add smoke-test or verifier-load instructions.

## Probe-purpose groups

Organize probes by why they exist, not only by hook type.

Valid purpose groups include:

* process lifecycle
* syscall auditing
* file-system activity
* network activity
* scheduler latency
* memory allocation
* container attribution
* security policy
* performance telemetry
* debugging-only instrumentation

Do not create or extend catch-all files such as:

```text
trace.bpf.c
probes.bpf.c
all_events.bpf.c
monitor.bpf.c
```

unless the existing repository already has a strong convention that cannot be changed safely.

Preferred layout:

```text
bpf/
  common/
    common.h
    events.h
    vmlinux_compat.h
  probes/
    process/
      process.bpf.c
      process_events.h
    fs/
      fs.bpf.c
      fs_events.h
    net/
      net.bpf.c
      net_events.h
    sched/
      sched.bpf.c
      sched_events.h
user/
  handlers/
    process.c
    fs.c
    net.c
    sched.c
```

If the repository has a different structure, adapt to it while preserving purpose segregation.

## Hook selection policy

Choose hooks in this order unless the user explicitly asks otherwise:

1. Tracepoints, when they expose enough stable data.
2. fentry/fexit, when BTF is available and function-level access is needed.
3. kprobes/kretprobes, only when tracepoints or fentry/fexit are unsuitable.
4. uprobes/uretprobes, for userspace binaries or libraries.
5. LSM hooks, only for security decision or enforcement logic.
6. XDP, tc, socket, or cgroup networking hooks, only for packet-path or network-policy logic.

Do not default to kprobes. Explain why a kprobe is necessary when using one.

## Naming rules

Use purpose-qualified names.

Good:

```c
fs__openat_enter
fs__openat_exit
process__execve_enter
process__execve_exit
net__tcp_connect
sched__wakeup
```

Bad:

```c
probe1
handle_event
trace_syscall
do_monitor
```

Maps must also be purpose-qualified.

Good:

```c
fs_events
fs_open_args
process_events
process_exec_args
net_conn_events
sched_latency_events
```

Bad:

```c
events
args_map
tmp
state
```

## Map isolation

Each purpose group owns its maps.

Do not reuse a generic correlation map across unrelated probe groups.

For syscall entry/exit correlation, use a purpose-specific map:

```c
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 8192);
    __type(key, __u64);
    __type(value, struct fs_open_args);
} fs_open_args SEC(".maps");
```

Use `bpf_get_current_pid_tgid()` for thread-specific correlation unless the repository uses a stronger existing key.

## Event schema isolation

Each purpose group should define its own event structs.

Example:

```c
struct fs_event {
    __u64 ts_ns;
    __u64 pid_tgid;
    __u64 uid_gid;
    __s32 ret;
    char comm[16];
    char path[256];
};
```

Avoid one giant global event union unless the repository already has a stable event ABI.

If a common header is needed, keep it minimal:

```c
struct event_header {
    __u64 ts_ns;
    __u32 type;
    __u32 size;
    __u32 pid;
    __u32 tgid;
};
```

## Memory access rules

For CO-RE kernel struct reads, prefer:

```c
BPF_CORE_READ(ptr, field)
```

For kernel memory reads, use:

```c
bpf_probe_read_kernel(dst, sizeof(*dst), src);
```

For userspace memory reads, use:

```c
bpf_probe_read_user_str(dst, sizeof(dst), user_ptr);
```

Never dereference kernel or userspace pointers directly unless the program type and context explicitly make it safe.

Check pointers before reading from them.

## Ring buffer event output

Prefer `BPF_MAP_TYPE_RINGBUF` for structured telemetry unless the project already uses perf buffers.

Pattern:

```c
struct fs_event *event;

event = bpf_ringbuf_reserve(&fs_events, sizeof(*event), 0);
if (!event)
    return 0;

__builtin_memset(event, 0, sizeof(*event));

event->ts_ns = bpf_ktime_get_ns();
event->pid_tgid = bpf_get_current_pid_tgid();
event->uid_gid = bpf_get_current_uid_gid();
bpf_get_current_comm(&event->comm, sizeof(event->comm));

bpf_ringbuf_submit(event, 0);
return 0;
```

Always initialize all event fields before submitting.

## Verifier-safety rules

Generated BPF code must avoid:

* unbounded loops
* large stack allocations
* unchecked pointer arithmetic
* variable-length memory access
* deeply nested branches
* shared scratch buffers across unrelated logic
* complex logic inside hot-path probes
* policy decisions inside telemetry probes

Prefer:

* fixed-size structs
* bounded copies
* early returns
* explicit null checks
* small helper functions
* purpose-specific maps
* simple control flow

## Policy and telemetry separation

Do not mix security enforcement and observability telemetry in the same probe group.

Use:

```text
bpf/probes/security/
```

for LSM or enforcement-oriented probes.

Use:

```text
bpf/probes/fs/
bpf/probes/process/
bpf/probes/net/
```

for observation and telemetry.

A security probe may emit audit events, but generic telemetry probes must not silently enforce policy.

## Tail-call rules

Use tail calls only for deliberate modular dispatch.

Rules:

* keep chains shallow
* document the program array map
* provide fallback behavior when a tail call misses
* do not hide unrelated purpose groups behind tail calls
* do not use tail calls as a replacement for clean source layout

## Userspace loader requirements

When adding a probe group, update userspace code so the group can be controlled independently.

Expected config shape:

```yaml
probes:
  process:
    enabled: true
  fs:
    enabled: true
  net:
    enabled: false
  sched:
    enabled: true
```

The loader should:

* load each purpose group independently where possible
* report load/attach errors per group
* avoid failing the whole application when an optional group is unavailable
* decode events using the matching purpose-specific schema
* expose dropped-event or ring-buffer pressure metrics when supported

## Required output when generating code

When implementing a new eBPF probe, provide:

1. BPF-side C code.
2. Event struct/header updates.
3. Map definitions.
4. Userspace loader changes.
5. Userspace event decoder changes.
6. Build-system changes if needed.
7. Explanation of hook choice.
8. Verifier-safety notes.
9. Smoke-test commands.

Do not return only a standalone BPF snippet unless explicitly requested.

## Review checklist

Before finalizing, verify:

* The probe has one clear purpose.
* The probe group is isolated from unrelated groups.
* Hook choice is justified.
* Stable tracepoints or fentry/fexit were considered before kprobes.
* Maps are purpose-specific.
* Event schemas are purpose-specific.
* Correlation state is not shared across unrelated logic.
* All event fields are initialized before submit.
* Kernel and userspace memory reads use correct helpers.
* CO-RE is used for kernel struct access when applicable.
* Loader can enable or disable the group independently.
* Control flow is verifier-friendly.
* Naming clearly identifies purpose and event.
* Comments document hook, event output, and kernel dependencies.

## Minimal BPF probe group template

Use this template for a new purpose group:

```c
// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_core_read.h>
#include <bpf/bpf_tracing.h>

#include "common/events.h"
#include "probes/<purpose>/<purpose>_events.h"

char LICENSE[] SEC("license") = "Dual BSD/GPL";

/*
 * Purpose: <short description>
 * Hooks:
 *   - <hook name>
 * Emits:
 *   - <event type>
 * State:
 *   - <maps used for correlation>
 * Kernel dependencies:
 *   - <tracepoint/BTF/helper requirements>
 */

struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 1 << 24);
} <purpose>_events SEC(".maps");

SEC("<attach-section>")
int <purpose>__<event_name>(void *ctx)
{
    struct <purpose>_event *event;

    event = bpf_ringbuf_reserve(&<purpose>_events, sizeof(*event), 0);
    if (!event)
        return 0;

    __builtin_memset(event, 0, sizeof(*event));

    event->ts_ns = bpf_ktime_get_ns();
    event->pid_tgid = bpf_get_current_pid_tgid();
    event->uid_gid = bpf_get_current_uid_gid();
    bpf_get_current_comm(&event->comm, sizeof(event->comm));

    bpf_ringbuf_submit(event, 0);
    return 0;
}
```

## When reviewing existing eBPF code

When asked to review or refactor existing eBPF code:

1. Identify mixed-purpose probes.
2. Propose a purpose-group split.
3. Move maps and event structs into the correct group.
4. Keep public event ABI compatibility unless explicitly allowed to break it.
5. Preserve existing loader behavior while adding per-group enablement.
6. Call out verifier risks before changing behavior.
7. Prefer small, reviewable patches.
