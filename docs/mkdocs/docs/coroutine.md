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

## Key Components

### High-level API (Primary Usage)

- `master.foreach_exchange()`: Executes a callback function with coroutine support, automatically handling multiple exchange rounds
- `cp.yield()`: Yields control from the current block to trigger a message exchange round
- Automatic lifecycle management: Coroutines are created and managed automatically for each block

### Low-level API (Advanced Usage)

- `diy::coroutine` namespace: Core functions for manual coroutine management
- `co_create()`, `co_switch()`, `co_delete()`: Manual coroutine creation, switching, and cleanup
- `diy::coroutine::argument()`: Thread-local argument passing mechanism

## High-level API

### foreach_exchange()

The `foreach_exchange()` method combines block processing with automatic message exchange handling:

```cpp
void foreach_exchange(const F& callback,
                    bool remote = false,
                    unsigned int stack_size = 16*1024*1024);
```

Parameters:

- `callback`: Function object called for each block with signature `void(Block*, const ProxyWithLink&)`
- `remote`: Whether to include remote communication (default: false)
- `stack_size`: Stack size allocated for each coroutine in bytes (default: 16MB)

### yield()

The `yield()` method is called within the callback to trigger a message exchange round:

```cpp
void yield() const;
```

When `yield()` is called:

- The current block pauses execution
- DIY performs a message exchange between all active blocks
- Execution resumes at the line following the `yield()` call

### Usage Pattern

Replace traditional foreach/exchange patterns with coroutine-based execution:

**Traditional approach:**
```cpp
master.foreach(&local_sum);          // First computation phase
master.exchange();                   // Message exchange
master.foreach(&average_neighbors);  // Second computation phase
```

**Coroutine approach:**
```cpp
master.foreach_exchange([](Block* const& b, const diy::Master::ProxyWithLink& cp)
{
    local_sum(b, cp);                // First computation phase
    cp.yield();                      // Trigger message exchange
    average_neighbors(b, cp);        // Second computation phase
});
```

## Low-level API

The low-level API provides direct control over coroutine creation and management. This is useful for advanced use cases requiring fine-grained control.

### Core Functions

```cpp
namespace diy::coroutine {
    using cothread_t = void*;

    cothread_t  co_active();                     // Get current coroutine
    cothread_t  co_create(unsigned int stack_size, void (*entry)(void));
    void        co_delete(cothread_t cothread);
    void        co_switch(cothread_t cothread);
    void*&      argument();                      // Thread-local argument storage
}
```

### Basic Usage Pattern

```cpp
// Create coroutine
auto coro = diy::coroutine::co_create(stack_size, &coroutine_function);

// Set up arguments
struct Info { diy::coroutine::cothread_t main; int value; };
Info info = { diy::coroutine::co_active(), 42 };
diy::coroutine::argument() = &info;

// Switch to coroutine
diy::coroutine::co_switch(coro);

// Clean up when done
diy::coroutine::co_delete(coro);
```

### Coroutine Function Structure

```cpp
void coroutine_function()
{
    Info* info = static_cast<Info*>(diy::coroutine::argument());
    diy::coroutine::cothread_t main = info->main;

    // First execution phase
    std::cout << info->value << std::endl;

    // Return to main
    diy::coroutine::co_switch(main);

    // Resume execution here
    std::cout << "Resumed execution" << std::endl;

    // Return to main and complete
    diy::coroutine::co_switch(main);
}
```

## Usage Examples

### High-level Example

From `examples/simple/simple.cpp`, the coroutine-based execution:

```cpp
master.foreach_exchange([](Block* const& b, const diy::Master::ProxyWithLink& cp)
{
    // First phase: sum local values
    local_sum(b, cp);
    cp.yield();                    // Yield for message exchange

    // Second phase: compute averages of received values
    average_neighbors(b, cp);
});
```

### Low-level Example

Simplified from `examples/coroutine/coroutine.cpp`, showing direct coroutine management:

```cpp
#include <diy/coroutine.hpp>
namespace dc = diy::coroutine;

struct Info
{
    dc::cothread_t  main;
    int             value;
};

void coroutine_function()
{
    Info* info = static_cast<Info*>(dc::argument());
    dc::cothread_t main = info->main;
    int x = info->value;

    // First execution
    std::cout << x << std::endl;
    x += 1;

    // Return to main
    dc::co_switch(main);

    // Resume execution
    std::cout << x << std::endl;
    dc::co_switch(main);
}

int main()
{
    // Create coroutine with 4MB stack
    auto coro = dc::co_create(4*1024*1024, &coroutine_function);

    Info info;
    info.main = dc::co_active();
    info.value = 5;

    dc::argument() = &info;

    std::cout << "Jumping to coroutine" << std::endl;
    dc::co_switch(coro);           // First switch
    std::cout << "Back in main" << std::endl;

    dc::co_switch(coro);           // Second switch
    dc::co_delete(coro);           // Clean up

    std::cout << "Done" << std::endl;
}
```

## When to Use Coroutines

Coroutines are beneficial when:

- Your algorithm requires multiple communication rounds between computation phases
- You want to express iterative algorithms more naturally within a single callback
- You need to pause execution at specific points to exchange messages
- You want to reduce code complexity compared to separate foreach/exchange calls

