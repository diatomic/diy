# Coroutines

Coroutines in DIY enable interleaving computation with communication, allowing blocks to yield control during execution and resume after message exchanges. This is particularly useful for algorithms that require multiple communication rounds between different phases of computation.

## Overview

In traditional DIY workflows, you typically execute computation phases and message exchanges sequentially:

```cpp
master.foreach(&phase1);       // Process all blocks
master.exchange();             // Exchange messages
master.foreach(&phase2);       // Process all blocks again
```

With coroutines, you can interleave these operations within a single callback function:

```cpp
master.foreach_exchange([](Block* b, const ProxyWithLink& cp)
{
    phase1(b, cp);            // First phase of processing
    cp.yield();               // Yield for message exchange
    phase2(b, cp);            // Second phase of processing
});
```

This approach simplifies code that requires multiple communication rounds and enables more natural expression of iterative algorithms.

- `master.foreach_exchange()`: Executes a callback function with coroutine support, automatically handling multiple exchange rounds
- `cp.yield()`: Yields control from the current block to trigger a message exchange round
- Automatic lifecycle management: Coroutines are created and managed automatically for each block

## foreach_exchange()

The `foreach_exchange()` method combines block processing with automatic message exchange handling:

```cpp
void foreach_exchange(const F&     callback,
                      bool         remote = false,
                      unsigned int stack_size = 16*1024*1024);
```

Parameters:

- `callback`: Function object called for each block with signature `void(Block*, const ProxyWithLink&)`
- `remote`: Whether to include remote communication (default: false)
- `stack_size`: Stack size allocated for each coroutine in bytes (default: 16MB)

## yield()

The `yield()` method is called within the callback to trigger a message exchange round:

```cpp
void yield() const;
```

When `yield()` is called:

- The current block pauses execution
- DIY performs a message exchange between all active blocks
- Execution resumes at the line following the `yield()` call

## Usage Pattern and Example

Replace traditional foreach/exchange pattern with coroutine-based execution:

**Traditional approach:**
```cpp
master.foreach(&local_sum);          // First computation phase
master.exchange();                   // Message exchange
master.foreach(&average_neighbors);  // Second computation phase
```

**Coroutine approach:** from `examples/simple/simple.cpp`
```cpp
master.foreach_exchange([](Block* const& b, const diy::Master::ProxyWithLink& cp)
{
    local_sum(b, cp);                // First computation phase
    cp.yield();                      // Trigger message exchange
    average_neighbors(b, cp);        // Second computation phase
});
```

## When to Use Coroutines

Coroutines are beneficial when:

- Your algorithm requires multiple communication rounds between computation phases
- You want to express iterative algorithms more naturally within a single callback
- You need to pause execution at specific points to exchange messages
- You want to reduce code complexity compared to separate foreach/exchange calls

