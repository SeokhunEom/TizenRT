# How to use Memory Leakage Checker

The Memory Leakage Checker shows leakage information of dynamic allocated memory at that time.  
It checks the entire RAM regions for an allocated memory address. If no region has that address,  
it is a leakage because it can not be released.  
This document describes how to enable and use the Leak Checker.

## How to enable

1. Enable *CONFIG_DEBUG_MM_HEAPINFO* using menuconfig as shown below:
```
Debug Options -> Enable Debug Output Features -> [*] Heap Info debug option
```
2. Enable *CONFIG_MEM_LEAK_CHECKER* and set *CONFIG_MEM_LEAK_CHECKER_HASH_TABLE_SIZE* value using menuconfig as shown below:
```
Application Configuration -> System Libraries and Add-Ons -> [*] Memory Leakage Checker
```

**Mem Leak Checker requires *arch/arm/include/<chip>/memory_region.h* to work. The selected board must have the memory_region.h file in its folder for the successful build.**

#### HASH_TABLE_SIZE

The Memory Leak Checker uses hash table to improve searching performance.  
CONFIG_MEM_LEAK_CHECKER_HASH_TABLE_SIZE represents the  number of hash table indexes.  
It has a good performance with big size but spends more memory.

Note: We recommand using a prime number. Default value of that configuration is 1007.

## How to use

This can be run on TASH.
```bash
TASH>> mem_leak [target]
       [target] is used only when APP_BINARY_SEPARATION is enabled.
       [target] is one of kernel, app1 or app2.


Type   |    Addr   |   Size   | Owner
--------------------------------------------
LEAK   | 0x202e540 |     1248 | 0x40d311c
LEAK   | 0x2030ec0 |     2352 | 0x40d3124
LEAK   | 0x2031800 |     3456 | 0x40d312c
LEAK   | 0x2032590 |     4576 | 0x40d3134
```

Here are description of result.

#### Type

There are two types, *LEAK* and *BROKEN*.  

*LEAK* means that *Addr* is a memory leakage and it was allocated by *Owner* with *Size*.  
*BROKEN* means that the given memory address has a wrong value in a chunk. It means memory is corrupted and is abnormal state.

#### Addr and Owner

*Addr* is an address allocated.  
*Owner* is the address of the code segment that requested the memory allocation. 

You can find a function symbol and a line number using *addr2line* command with owner address.
For FLAT_BUILD or kernel, you can use tinyara.elf file.
For APP_BINARY_SEPARATION, you can use app_dbg.elf file.
```bash
$ addr2line -e <ELF_PATH> 0x40d301c

.../TizenRT/apps/examples/hello/hello_main.c:71
```

## Graph and report semantics

The checker uses root-seeded reachability from registered roots and follows candidate-to-candidate
edges. Self-references, cycles, detached tails, convergence, and cross-heap links are
classified as retained allocation groups rather than being treated as incoming references.
Aligned exact pointers are definite roots; interior and unaligned matches remain
ambiguous and are reported separately. One-past pointers, allocator headers, padding,
freed nodes, checker workspace, and control ranges are excluded from the candidate set.
Reports retain allocation capacity and requested size, bound copied data to 32 bytes,
and preserve the legacy five-column row for definite leaks alongside provenance and SCC
detail records.

## Reproducible validation and hardware policy

The canonical final validation commands are:

```bash
tools/mem_leak_checker_qa.sh qemu --task 15 --commands kernel_tc,mem_leak --repeat-mem-leak 2
tools/mem_leak_checker_qa.sh build --task 15 --config rtl8730e/flat_apps --name rtl-flat
tools/mem_leak_checker_qa.sh build --task 15 --config rtl8730e/loadable_apps --name rtl-loadable
```

For this execution, physical hardware discovery, deployment, runtime probing, and fatal
reboot validation are intentionally skipped. Receipts must carry
`hardware_validation: "skipped_by_user"`; QEMU remains
`deferred_unexecuted_baseline_link_failure`, and no deferred result is a runtime PASS.
The task-15 contract records GPTM3 for QEMU, CNTVCT for RTL, the 100000-usec measured
deadline target, protected operation budgets, and the runtime identity note exclusion
from payload hashing. Flash manifests describe only repository `ALL` partitions; build
intermediates are provenance and are never programmed.
