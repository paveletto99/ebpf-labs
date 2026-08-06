# Probes


## Core idea: probe logic should be grouped by intent, not by hook type

A common mistake is organizing eBPF code like this:

```text
tracepoints/
kprobes/
uprobes/
lsm/
tc/
xdp/
```

That mirrors the kernel attachment mechanism, but it does not mirror the product logic.

A better production layout is:

```text
probes/
  process/
    exec.bpf.c
    exit.bpf.c
    fork.bpf.c
  file/
    open.bpf.c
    unlink.bpf.c
    rename.bpf.c
  network/
    connect.bpf.c
    accept.bpf.c
    dns.bpf.c
  container/
    namespace.bpf.c
    cgroup.bpf.c
  security/
    lsm_file_open.bpf.c
    lsm_bprm_check.bpf.c
  common/
    maps.bpf.h
    events.bpf.h
    task.bpf.h
    filters.bpf.h
```

Why? Because your actual questions are usually:

```text
What process started?
What file was touched?
What network connection was made?
What security decision was attempted?
```

Not:

```text
Which tracepoint fired?
```

The attachment type is an implementation detail.

---

## Probe taxonomy

At a high level, eBPF probes fall into several families.

### 1. Tracepoints

Tracepoints are stable-ish kernel instrumentation points.

Example purposes:

```text
sys_enter_execve
sys_exit_execve
sched_process_exit
sched_process_fork
```

They are good when you want kernel-provided structured context.

Use them for:

```text
process lifecycle
syscall visibility
scheduler events
block I/O events
network stack events
```

Tradeoff: they expose only what the tracepoint gives you.

---

### 2. Kprobes / kretprobes

Kprobes attach to kernel functions.

Useful when no tracepoint exists or when you need deeper internals.

Example:

```text
kprobe/security_file_open
kretprobe/do_sys_openat2
kprobe/tcp_v4_connect
```

Use them when you need:

```text
function arguments
return values
kernel internal behavior
older kernel support
```

Tradeoff: less stable. Function names and signatures may change across kernel versions.

For production, prefer:

```text
tracepoint > LSM > fentry/fexit > kprobe
```

when possible.

---

### 3. fentry / fexit

These are BPF trampoline-based hooks into kernel functions.

Compared to kprobes, they are faster and type-aware when BTF is available.

Example:

```c
SEC("fentry/tcp_v4_connect")
int BPF_PROG(handle_tcp_connect, struct sock *sk)
{
    return 0;
}
```

Use them when:

```text
BTF is available
you want lower overhead
you need typed kernel function arguments
```

Tradeoff: less portable to older kernels without BTF.

---

### 4. LSM probes

LSM hooks are excellent for security semantics.

Example:

```c
SEC("lsm/file_open")
int BPF_PROG(handle_file_open, struct file *file)
{
    return 0;
}
```

Use them for:

```text
security decisions
policy visibility
access checks
runtime detection
```

LSM probes often answer better questions than syscall probes.

For example, instead of asking:

```text
Did someone call openat?
```

you ask:

```text
Did the kernel security layer evaluate opening this file?
```

That is often closer to detection logic.

---

### 5. Uprobes / uretprobes

Uprobes attach to userspace functions.

Use them for:

```text
library calls
language runtime behavior
application-level protocols
OpenSSL reads/writes
database client calls
malloc/free profiling
```

Example targets:

```text
libssl.so:SSL_write
libc.so:connect
python interpreter functions
JVM symbols
```

Tradeoff: symbol/version/deployment complexity.

---

### 6. XDP / TC

These are packet-path programs.

Use XDP for very early packet handling at driver/NIC ingress.

Use TC for richer skb-level context, ingress/egress shaping, metadata, and policy.

For detection/observability, TC is usually more ergonomic. For ultra-low-latency packet filtering, XDP is better.

---

## Good segregation model

Think in layers:

```text
domain
  ├── hook adapters
  ├── normalization
  ├── filtering
  ├── enrichment
  └── event emission
```

Example for process execution:

```text
process/exec.bpf.c

hook adapters:
  tracepoint/syscalls/sys_enter_execve
  tracepoint/sched/sched_process_exec
  lsm/bprm_check_security

normalization:
  extract pid, tgid, uid, gid, comm, argv, cgroup, namespaces

filtering:
  ignore host processes?
  ignore own agent?
  include only containers?

enrichment:
  container id
  mount namespace
  pid namespace
  parent process info

event emission:
  process_exec_event
```

The hook-specific part should be thin.

The domain logic should be reusable.

---

## Pattern: thin hook, shared domain handler

Bad:

```c
SEC("tracepoint/syscalls/sys_enter_openat")
int trace_openat(struct trace_event_raw_sys_enter *ctx)
{
    // read args
    // parse path
    // apply filters
    // build event
    // submit event
    return 0;
}

SEC("lsm/file_open")
int lsm_file_open(struct file *file)
{
    // different copy of filters
    // different event shape
    // different path logic
    return 0;
}
```

Better:

```c
static __always_inline int handle_file_open(struct file_event_ctx *ctx)
{
    if (should_ignore_current_task())
        return 0;

    struct file_event *event = reserve_file_event();
    if (!event)
        return 0;

    fill_task_context(&event->task);
    fill_file_context(ctx, &event->file);

    submit_file_event(event);
    return 0;
}
```

Then each hook only adapts its input:

```c
SEC("lsm/file_open")
int BPF_PROG(on_lsm_file_open, struct file *file)
{
    struct file_event_ctx ctx = {};
    ctx.file = file;
    ctx.source = EVENT_SOURCE_LSM_FILE_OPEN;

    return handle_file_open(&ctx);
}
```

For syscall tracepoint:

```c
SEC("tracepoint/syscalls/sys_enter_openat")
int on_sys_enter_openat(struct trace_event_raw_sys_enter *ctx)
{
    struct file_event_ctx fctx = {};
    fctx.filename_ptr = (const char *)ctx->args[1];
    fctx.source = EVENT_SOURCE_SYSCALL_OPENAT;

    return handle_file_open(&fctx);
}
```

Now your segregation is based on **file-open semantics**, not on LSM vs tracepoint.

---

## Suggested grouping by purpose

For runtime security / Falco-like detections, I’d group probes like this:

```text
process lifecycle
  exec
  fork/clone
  exit
  credentials changes
  namespace changes

file activity
  open
  create
  write
  chmod/chown
  unlink
  rename
  mount

network activity
  socket create
  bind
  listen
  accept
  connect
  send/recv metadata
  DNS/TLS enrichment if needed

identity and privilege
  setuid/setgid
  capabilities
  ptrace
  seccomp
  module loading

container context
  cgroup attach
  namespace IDs
  overlayfs/container root
  Kubernetes metadata correlation in userspace

security decisions
  LSM hooks
  denied operations
  policy-relevant access attempts
```

The actual attachment points are selected underneath each domain.

---

## Rule of thumb

For each probe group, define this contract:

```c
event_type
domain
minimum kernel version
preferred hook
fallback hook
event schema
maps used
drop behavior
expected cardinality
```

Example:

```text
Domain: process.exec
Preferred hook: tracepoint/sched/sched_process_exec
Fallback: tracepoint/syscalls/sys_enter_execve + sys_exit_execve
Event: process_exec_event
Maps:
  exec_args_heap
  task_cache
  container_cache
Failure policy:
  emit truncated argv if full args cannot be captured
```

This keeps the design maintainable.

---

## Mini exercise

Let’s start with one probe family.

Suppose you need to capture **process execution** for runtime detection.

You have at least three possible hook choices:

```text
tracepoint/syscalls/sys_enter_execve
tracepoint/sched/sched_process_exec
lsm/bprm_check_security
```

Which one would you choose as the **primary source of truth**, and what would you use the others for?
