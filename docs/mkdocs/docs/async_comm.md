## Asynchronous `iexchange`

Rather than synchronizing between computations and message exchanges as in [local synchronous communication](local_comm.md), `diy::Master::iexchange` attempts to interleave
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
    for (int i = 0; i < l->size(); ++i)
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
