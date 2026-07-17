package generated

import "github.com/cilium/ebpf"

type BpfObjects = bpfObjects

func LoadBpfObjects(obj *BpfObjects, opts *ebpf.CollectionOptions) error {
	return loadBpfObjects(obj, opts)
}
