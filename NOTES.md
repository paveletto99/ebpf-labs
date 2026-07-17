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