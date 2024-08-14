DIY supports synchronous local communication among neighboring blocks using
`diy::Master::exchange()`.  Additional lightweight
collectives can also be executed over the communication proxy of the synchronous `exchange` mechanism.

## Neighboring blocks connected by `diy::Master::ProxyWithLink`

In all of the following communication patterns, *local* means that a block communicates only with its "neighboring"
blocks. The neighborhood is defined as the collection of edges in a communication graph from the current block to some
other blocks; information is exchanged over these edges. In DIY, the *communication proxy* encapsulates the local
communicator over the neighborhood. The communication proxy includes the *link*, which is the collection of edges to the
neighboring blocks. Combined, the communication proxy and the link are contained in `diy::Master::ProxyWithLink`.  This
size of the link is the number of communication graph edges (i.e., neighboring blocks), and `link->target[i]` is the
block connected to the i-th edge. Edges can be added or removed dynamically. Data can be enqueued to and dequeued from
any target in the link as desired.

## Synchronous `exchange`

The synchronous `exchange` protocol has three steps:

- a callback function enqueues data to some of the blocks in its link (perhaps after peforming some local computation)
- `diy::Master::exchange` synchronously exchanges message queues
- another callback function dequeues the received data from its link (and perhaps performs some local computation)

~~~~{.cpp}
void foo(Block* b,                             // local block
         const diy::Master::ProxyWithLink& cp) // communication proxy for neighbor blocks
{
    diy::Link*    l = cp.link();                     // link to the neighbor blocks

    // compute some local value
    ...

    // for all neighbor blocks, enqueue data going to this neighbor block in the next exchange
    for (int i = 0; i < l->size(); ++i)
        cp.enqueue(l->target(i), value);
}

void bar(Block* b,                             // local block
         const diy::Master::ProxyWithLink& cp) // communication proxy for neighbor blocks
{
    diy::Link*    l = cp.link();

    // for all neighbor blocks, dequeue data received from this neighbor block in the last exchange
    for (int i = 0; i < l->size(); ++i)
    {
        int v;
        cp.dequeue(l->target(i).gid, v);
    }

    // compute some local value
    ...
}

int main(int argc, char**argv)
{
  ...

  master.foreach(&foo);
  master.exchange();
  master.foreach(&bar);
}
~~~~

Recall that `master.foreach()` executes the functions `foo` and `bar` on all of the blocks in the current MPI process,
so all blocks have the opportunity to enqueue and dequeue data to/from their neighbors. Because `master.exchange` is
called explicitly, it synchronizes all of the blocks in between calling `foo` and `bar`, effectively acting as a
barrier.

## Proxy collectives for `exchange`

When a lightweight reduction needs to be mixed into an existing pattern such as a neighborhood exchange, DIY has a
mechanism for this. An example is a neighbor exchange that must iterate until the collective result indicates it is time
to terminate (as in particle tracing in rounds until no block has any more work to do). The underlying mechanism works
as follows.

- The input values from each block are pushed to a vector of inputs for the process.
- The corresponding MPI collective is called by the process.
- The reduced results are redistributed to the process' blocks.

The inputs are pushed by calling `all_reduce` from each block, and the outputs are popped by calling `get` from each
block. The collective mechanism has the following characteristics.

- It is a simple member function of the communication proxy that is piggybacked onto the underlying proxy, not a completely new pattern.
- The reduction works per block and can be out of core because it uses the underlying proxy.
- The operator is a simple predefined macro. The following operators are available: `maximum<T>`, `minimum<T>`,
  `std::plus<T>`, `std::multiplies<T>`, `std::logical_and<T>`, and `std::logical_or<T>`.
- The result is retrieved using the `get` function, not dequeue.
- Currently, all_reduce is the only collective available.

A code snippet is below. The complete example is [here](https://github.com/diatomic/diy/blob/master/examples/simple/simple.cpp)

~~~~{.cpp}
void foo(Block* b,                             // local block
         const diy::Master::ProxyWithLink& cp) // communication proxy
{

  ...

  cp.all_reduce(value, std::plus<int>());      // local value being reduced
}

void bar(Block* b,                             // local block
         const diy::Master::ProxyWithLink& cp) // communication proxy
{

  int total = cp.get<int>();                   // result of the reduction

  ...
}

int main(int argc, char**argv)
{
  ...

  master.foreach(&foo);
  master.exchange();
  master.foreach(&bar);
}
~~~~

