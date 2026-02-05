# Dynamic Load Balancing

Dynamic load balancing automatically redistributes blocks across processes to balance computational workload. This is useful when different blocks require different amounts of computation time, leading to some processes finishing much earlier than others.

## Overview

In many parallel applications, the computational cost of processing different blocks varies significantly. Without load balancing, some processes may become idle while others are still processing their expensive blocks. DIY's dynamic load balancing solves this by:

- Monitoring workload across processes
- Identifying overloaded processes (those with blocks requiring high computation)
- Moving blocks from overloaded to underloaded processes
- Continuing computation with improved load balance

Dynamic load balancing is most beneficial in applications where:

- Different blocks have varying computational complexity
- The computational cost cannot be easily predicted beforehand
- The workload may change during execution

## Key Components

### DynamicAssigner

The `diy::DynamicAssigner` is a special assigner that tracks the current location of blocks as they move between processes during load balancing.

```{.cpp}
diy::DynamicAssigner dynamic_assigner(world, world.size(), nblocks)
```

Parameters:

- `world`: MPI communicator
- `world.size()`: Total number of MPI processes
- `nblocks`: Total number of blocks in the global domain

After creating the blocks, record their initial locations:

```{.cpp}
diy::record_local_gids(master, dynamic_assigner)
```

### Work Estimation Function

A callback function that returns the estimated computational work for each block. The work can be any user-defined measure (e.g., number of operations, estimated time, complexity units).

```{.cpp}
diy::Work get_block_work(Block* block, int gid)
{
    return block->estimated_work;
}
```

### Compute Callback Function

A user-defined function that performs the actual computation on each block. This is the same type of function used with regular `foreach()`.

```{.cpp}
void compute(Block* b, const diy::Master::ProxyWithLink& cp, int max_time)
{
    // Perform the actual computation for this block
    // Use b->data to access block data
    // Use cp for communication if needed
}
```

### Dynamic Foreach

The `master.dynamic_foreach()` method combines computation with automatic load balancing. It executes blocks while monitoring workload and moving blocks between processes as needed.

## API Reference

### diy::DynamicAssigner Constructor

```{.cpp}
DynamicAssigner(const mpi::communicator& comm, int size, int nblocks)
```

### record_local_gids()

```{.cpp}
void record_local_gids(const Master& master, DynamicAssigner& dynamic_assigner)
```

### master.dynamic_foreach()

```{.cpp}
void dynamic_foreach(const F& compute_function,
                     const G& work_function,
                     DynamicAssigner& dynamic_assigner,
                     float sample_frac,
                     float quantile)
```

Parameters:

- `compute_function`: Callback function that performs computation on each block
- `work_function`: Callback function that returns estimated work for each block
- `dynamic_assigner`: DynamicAssigner object that tracks block locations
- `sample_frac`: Fraction of processes to sample for load information (0.0-1.0)
- `quantile`: Cutoff threshold above which blocks are considered for moving (0.0-1.0)

### Parameter Recommendations

**sample_frac**: Controls the fraction of processes sampled for load information

- `0.5f` (default): Sample half the processes - good balance between accuracy and communication overhead
- Lower values (e.g., `0.3f`): Less communication overhead but less accurate load information
- Higher values (e.g., `0.8f`): More accurate load information but higher communication overhead

**quantile**: Controls the threshold for identifying overloaded blocks

- `0.8f` (default): Move blocks from top 20% most loaded processes - moderate load balancing
- Lower values (e.g., `0.6f`): More aggressive load balancing, moves more blocks
- Higher values (e.g., `0.9f`): Less aggressive load balancing, moves fewer blocks

<!--
## Usage Steps

### Step 1: Create and Initialize DynamicAssigner

```{.cpp}
// Create dynamic assigner
diy::DynamicAssigner dynamic_assigner(world, world.size(), nblocks);

// After creating blocks and adding them to master, record their locations
diy::record_local_gids(master, dynamic_assigner);
world.barrier();  // Synchronize before starting dynamic execution
```

### Step 2: Implement Compute Function

```{.cpp}
void foo(Block* b,                             // local block
         const diy::Master::ProxyWithLink& cp) // communication proxy for neighbor blocks
{
    ...
}
```

### Step 3: Implement Work Estimation Function

```{.cpp}
diy::Work get_block_work(Block* block, int gid)
{
    DIY_UNUSED(gid);
    return block->estimated_work;  // Return estimated work for this block
}
```

### Step 4: Call dynamic_foreach

```{.cpp}
master.dynamic_foreach(
    [&](Block* b, const diy::Master::ProxyWithLink& cp)
    { compute(b, cp, max_time); },          // compute function
    &get_block_work,                        // work estimation function
    dynamic_assigner,                       // dynamic assigner
    0.5f,                                   // sample fraction
    0.8f);                                  // quantile
```
-->

## Complete Example

Below are the key snippets from `examples/load_balancing/dynamic.cpp` showing the essential load balancing components.

### Block Structure with Work Information

```{.cpp}
struct Block
{
    static void* create()            { return new Block; }
    static void  destroy(void* b)    { delete static_cast<Block*>(b); }

    // Block data
    int                 gid;
    diy::Work           estimated_work;      // Estimated work for this block
    // ... other user data
};
```

### Work Assignment

```{.cpp}
// Assign work to blocks (e.g., based on data size, complexity, etc.)
master.foreach([&](Block* b, const diy::Master::ProxyWithLink& cp)
               { b->assign_work(cp, 0, noise_factor, distribution, generator); });
```

### Work Estimation Function

```{.cpp}
diy::Work get_block_work(Block* block, int gid)
{
    DIY_UNUSED(gid);
    return block->estimated_work;
}
```

### Compute Function

```{.cpp}
void Block::compute(const diy::Master::ProxyWithLink&, int max_time, int)
{
    // Simulate computation proportional to work amount
    unsigned int usec = max_time * act_work * 10000L;
    std::this_thread::sleep_for(std::chrono::microseconds(usec));
}
```

### Dynamic Load Balancing Execution

```{.cpp}
// Create and initialize dynamic assigner
diy::DynamicAssigner dynamic_assigner(world, world.size(), nblocks);
diy::record_local_gids(master, dynamic_assigner);
world.barrier();

// Execute with dynamic load balancing
master.dynamic_foreach(
    [&](Block* b, const diy::Master::ProxyWithLink& cp) 
    { b->compute(cp, max_time, n); },        // compute function
    &get_block_work,                         // work estimation function
    dynamic_assigner,                        // dynamic assigner
    sample_frac,                             // sample fraction (e.g., 0.5f)
    quantile);                               // quantile (e.g., 0.8f)
```

The dynamic load balancing will automatically move blocks from overloaded processes to underloaded ones during execution, improving overall performance and utilization.
