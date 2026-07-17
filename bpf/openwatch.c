// go:build ignore

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char LICENSE[] SEC("license") = "Dual MIT/GPL";

struct event
{
    __u32 pid;
    __s32 ret;
    __u64 latency_ns;
    char comm[16];
    char filename[256];
};

struct
{
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 1 << 24);
} events SEC(".maps");

struct enter_args
{
    unsigned long long unused;
    long syscall_nr;
    long dfd;
    const char *filename;
    long flags;
    long mode;
};

struct exit_args
{
    unsigned long long unused;
    long syscall_nr;
    long ret;
};

struct inflight
{
    __u64 start_ns;
    char filename[256];
};

struct
{
    __uint(type, BPF_MAP_TYPE_LRU_HASH);
    __uint(max_entries, 10240);
    __type(key, __u32);
    __type(value, struct inflight);
} starts SEC(".maps");

struct
{
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u64);
} lookup_misses SEC(".maps");

struct
{
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u64);
} ringbuf_drops SEC(".maps");

SEC("tracepoint/syscalls/sys_enter_openat")
int handle_enter(struct enter_args *ctx)
{
    __u32 tid = bpf_get_current_pid_tgid();
    struct inflight st = {};
    st.start_ns = bpf_ktime_get_ns();
    bpf_probe_read_user_str(&st.filename, sizeof(st.filename), ctx->filename);
    bpf_map_update_elem(&starts, &tid, &st, BPF_ANY);
    return 0;
}

SEC("tracepoint/syscalls/sys_exit_openat")
int handle_exit(struct exit_args *ctx)
{
    __u64 pid_tgid = bpf_get_current_pid_tgid();
    __u32 tid = (__u32)pid_tgid;
    __u32 pid = pid_tgid >> 32;

    struct inflight *st = bpf_map_lookup_elem(&starts, &tid);
    if (!st)
        return 0;

    struct inflight *state = bpf_map_lookup_elem(&starts, &tid);
    if (!state)
    {
        // increment lookup misses
        return 0;
    }
    __u64 latency_ns =
        bpf_ktime_get_ns() - state->start_ns;

    // int i = 1000000;

    // if (latency_ns <= i)
    // {
    //     bpf_map_delete_elem(&starts, &tid);
    //     return 0;
    // }

    struct event *e = bpf_ringbuf_reserve(&events, sizeof(*e), 0);
    if (!e)
    {
        bpf_map_delete_elem(&starts, &tid);
        return 0;
    }

    e->pid = pid;
    e->ret = (__s32)ctx->ret;
    e->latency_ns = latency_ns;

    bpf_get_current_comm(&e->comm, sizeof(e->comm));
    __builtin_memcpy(&e->filename, st->filename, sizeof(e->filename));

    bpf_ringbuf_submit(e, 0);
    bpf_map_delete_elem(&starts, &tid);
    return 0;
}