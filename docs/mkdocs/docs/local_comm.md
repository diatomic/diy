DIY supports two local communication patterns among neighboring blocks: The first is synchronous, using
`diy::Master::exchange()`, and the second is asynchronous, using `diy::Master::iexchange()`.  Additional lightweight
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
    for (int i = 0; i < l.size(); ++i)
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

## Asynchronous `iexchange`

Rather than synchronizing between computations and message exchanges, `diy::Master::iexchange` attempts to interleave
computation and communication and execute the three steps (enqueue, exchange, dequeue) asynchronously and repeatedly
until there is no more work left to do and no more messages left in the system. This protocol is for iterative
computations that eventually converge, where the result is independent of the order of message arrival and does not
require strict synchronization between iterations. While this may seem restrictive, many algorithms, e.g., parallel
particle tracing, distributed union-find, and many graph algorithms qualify. Moreover, the global amount of work does
not need to monotonically decrease over the execution of the algorithm; new work can suddenly appear, and `iexchange`
will continue to run until the system state is quiet, automatically determining when to terminate. This is exceptionally
useful for data-driven, irregular, iterative algorithms. In these cases, the performance of `iexchange` is often better
than `exchange`, and it can be easier to use.

There is only one callback function required, and it is passed directly to `iexchange`. The body of the callback
function includes both dequeuing and enqueuing. DIY handles the messaging internally as it calls this function repeatedly
until the function returns that its local work is done, and DIY confirms that there are no pending messages in flight.

~~~~{.cpp}
// callback for asynchronous iexchange
// return: true = I'm done unless more work arrives; false = I'm not done, please call me again
bool foo(Block* b,                             // local block
         const diy::Master::ProxyWithLink& cp) // communication proxy for neighbor blocks
{
    diy::Link*    l = cp.link();               // link to the neighbor blocks

    // compute some local value
    ...

    // for all neighbor blocks, enqueue data going to this neighbor block
    for (int i = 0; i < l->size(); ++i)
        cp.enqueue(l->target(i), value);

    // for all neighbor blocks, dequeue data received from this neighbor block
    for (int i = 0; i < l.size(); ++i)
    {
        int v;
        while (cp.incomingl(l->target(i).gid))   // get everything available
            cp.dequeue(l->target(i).gid, v);
    }

    // compute some local value
    ...

    // determime whether this block is done for now
    bool done = ...;

    return done;
}

int main(int argc, char**argv)
{
  ...

  master.iexchange(&foo);
}
~~~~

A complete example is [here](https://github.com/diatomic/diy/blob/master/examples/simple/iexchange-particles.cpp)
