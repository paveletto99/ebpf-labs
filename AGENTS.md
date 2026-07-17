# AGENTS.md

## eBPF work

For Linux kernel eBPF probe work, use the [`ebpf-probe-writer`](.agents/skills/ebpf-probe-writer/SKILL.md) skill.

Probe logic must be segregated by purpose. Do not add unrelated probes, maps, event schemas, or userspace decoders to a shared catch-all implementation.