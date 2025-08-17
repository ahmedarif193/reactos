# Kernel Stack Size Impact Analysis

## Can We Set Any Stack Size?

**Short answer**: Technically yes, but with significant consequences.

## What Happens With Different Stack Sizes?

### Current Sizes
| Architecture | Size | Hex | Memory Per Thread |
|-------------|------|-----|-------------------|
| i386 | 12 KB | 0x3000 | 12 KB |
| AMD64 MSVC | 24 KB | 0x6000 | 24 KB |
| AMD64 MinGW (fixed) | 32 KB | 0x8000 | 32 KB |

### If We Set 1MB Stack (0x100000)

## Memory Impact

### System with 100 Threads
- **Current (32KB)**: 100 × 32KB = 3.2 MB
- **With 1MB stacks**: 100 × 1MB = 100 MB
- **Overhead**: 96.8 MB wasted

### System with 1000 Threads (heavy load)
- **Current (32KB)**: 1000 × 32KB = 32 MB
- **With 1MB stacks**: 1000 × 1MB = 1 GB
- **Overhead**: 968 MB wasted

### Real ReactOS Boot (typical)
- ~200 threads during boot
- **Current (32KB)**: 200 × 32KB = 6.4 MB
- **With 1MB stacks**: 200 × 1MB = 200 MB
- **31x more memory usage**

## Performance Impact

### 1. TLB (Translation Lookaside Buffer) Pressure
```
Larger stacks = More page table entries = More TLB misses
```
- Each 1MB stack needs 256 × 4KB pages
- TLB typically holds 512-1536 entries
- Just 6 threads would overflow the TLB

### 2. Cache Impact
```
Stack Size | L1 Cache Coverage | L2 Cache Coverage | L3 Cache Coverage
-----------|------------------|-------------------|------------------
32 KB      | Full coverage    | Full coverage     | Full coverage
1 MB       | 3% coverage      | 12.5% coverage    | Partial coverage
```
- L1 cache: 32-64 KB (1MB stack won't fit)
- L2 cache: 256-512 KB (1MB stack won't fit)
- L3 cache: 8-32 MB (multiple 1MB stacks compete)

### 3. Page Fault Overhead
- Kernel stacks are committed memory (not demand-paged)
- 1MB stack = 256 pages to allocate and zero
- Stack allocation becomes 32x slower

## Physical Memory Limits

### On 2GB System (LiveCD minimum)
- **OS overhead**: ~500 MB
- **Available for apps**: ~1.5 GB
- **With 1MB stacks**: Could only support ~1500 threads total
- **With 32KB stacks**: Can support ~48,000 threads

### Memory Fragmentation
- 1MB contiguous allocations fragment memory faster
- Harder to find 1MB contiguous blocks
- System may fail to create threads despite having memory

## CPU Architecture Limits

### AMD64 Virtual Address Space
```
Kernel space: 128TB (Windows), 64TB (Linux)
Stack allocation: No hard limit, but...
```

### Page Table Overhead
Each 1MB stack requires:
- 1 PTE (Page Table Entry) per 4KB = 256 PTEs
- 256 PTEs × 8 bytes = 2KB page table overhead
- 1000 threads = 2MB just for page tables

## Real-World Problems with Large Stacks

### 1. NUMA Systems
- Stacks allocated on wrong NUMA node
- 1MB cross-node access = severe performance penalty
- 32KB fits in local node cache

### 2. Virtualization
- VMware/Hyper-V must track more memory
- Live migration takes longer
- Memory ballooning less effective

### 3. Security
- Larger attack surface for stack-based exploits
- Stack spraying attacks more effective
- Guard pages less effective (relatively)

## Why Kernels Use Small Stacks

### Linux Kernel
- **x86**: 8KB (2 pages)
- **x86_64**: 16KB (4 pages)
- Explicitly designed to be small

### Windows Kernel
- **x86**: 12KB
- **x64**: 24KB
- Carefully tuned over decades

### Reasoning
1. **Most functions use <1KB stack**
2. **Deep call chains are bugs** (should be iterative)
3. **Forces good coding practices**
4. **Improves cache locality**

## Stack Size Sweet Spots

### Recommended Sizes
```c
#ifdef _M_AMD64
  #ifdef __GNUC__
    #define KERNEL_STACK_SIZE 0x8000   // 32KB - Good for MinGW
    // #define KERNEL_STACK_SIZE 0xC000 // 48KB - If 32KB still fails
    // #define KERNEL_STACK_SIZE 0x10000 // 64KB - Maximum reasonable
  #else
    #define KERNEL_STACK_SIZE 0x6000   // 24KB - MSVC optimal
  #endif
#endif
```

### Never Exceed
- **64KB (0x10000)**: Maximum reasonable kernel stack
- **128KB (0x20000)**: Emergency/debugging only
- **1MB (0x100000)**: Will cause severe problems

## Testing Different Sizes

### How to Test Impact
```bash
# Change stack size
echo "#define KERNEL_STACK_SIZE 0x10000" >> config.h

# Monitor memory usage
cat /proc/meminfo | grep Slab

# Check thread creation time
time for i in {1..1000}; do CreateThread(); done

# Watch page faults
vmstat 1
```

## Conclusion

### Why 32KB (0x8000) is Optimal for AMD64 MinGW

1. **Sufficient**: Handles MinGW's 2.5x overhead
2. **Efficient**: 8 pages, good TLB usage
3. **Cache-friendly**: Fits in L2 cache
4. **Proven**: Similar to Windows x64 (24KB) + safety margin

### Why 1MB Would Be Disastrous

1. **Memory waste**: 31x more memory per thread
2. **Performance**: Cache misses, TLB thrashing
3. **Scalability**: Limits thread creation
4. **Compatibility**: Breaks assumptions in kernel code

### Best Practice
- Use minimum stack that prevents overflow
- 32KB for MinGW AMD64 is optimal
- Monitor with "Close to our death" warnings
- Increase only if warnings persist

## Formula for Stack Size Selection

```
Optimal Stack = Max(
    Measured Peak Usage × 1.5,  // 50% safety margin
    Architecture Minimum,        // Platform requirements
    Compiler Overhead           // PSEH, frame pointers
)

Where:
- Measured Peak: Use stack profiling tools
- Safety Margin: 1.5x for production, 2x for debug
- Never exceed: 16 × Measured Peak (waste)
```