Operations performed on blocks are done by passing callback functions to `diy::Master::foreach`

## Example

Assume the block is defined as:

~~~~{.cpp}
struct Block
{
    std::vector<int>      values;
    float                 average;
};
~~~~

An example of a callback function that sums the values in the local block and then enqueues the sum to its neighbors is
below.  The communication proxy `diy::Master::ProxyWithLink` is a local "communicator" to the neighboring blocks. This
communication proxy is the result of blocks with the same associated link having been previously added to the `Master`,
typically by the `Decomposer`. From the communication proxy, we can extract the `diy::Link`, which is the actual
communication subgraph linking the neighbors. By iterating over the link, we can enqueue data to particular neighbors.
Note that enqueueing only copies data to the message queue destined for the target block; no data are actually sent yet.

~~~~{.cpp}
// compute sum of local values and enqueue the sum
void local_sum(Block* b,                             // local block
               const diy::Master::ProxyWithLink& cp) // communication proxy for neighbor blocks
{
    diy::Link*    l = cp.link();                     // link to the neighbor blocks

    // compute local sum
    int total = 0;
    for (unsigned i = 0; i < b->values.size(); ++i)
        total += b->values[i];

    // for all neighbor blocks, enqueue data going to this neighbor block in the next exchange
    for (int i = 0; i < l->size(); ++i)
        cp.enqueue(l->target(i), total);
}
~~~~

An example of a callback function that dequeues values from its neighbors and averages them is below. Note that
dequeueing copies data from the message queues of source blocks; the data have already been sent.

~~~~{.cpp}
// dequeue values received from neighbors and average them
void average_neighbors(Block* b,                             // local block
                       const diy::Master::ProxyWithLink& cp) // communication proxy for neighbor blocks
{
    diy::Link*    l = cp.link();

    // for all neighbor blocks, dequeue data received from this neighbor block in the last exchange
    int total = 0;
    for (int i = 0; i < l.size(); ++i)
    {
        int v;
        cp.dequeue(l->target(i).gid, v);
        total += v;
    }

    // compute average
    b->average = float(total) / l.size();
}
~~~~

In between calling the above two functions, data are typically exchanged by `diy:Master` in the following pattern:

~~~~{.cpp}
    ...
    master.foreach(&local_sum);                         // compute sum of local values and enqueue to neighbors
    master.exchange();                                  // exchange enqueued data between neighbor blocks
    master.foreach(&average_neighbors);                 // dequeue values received from neighbors and average them
    ...
~~~~

Callback functions can always have additional user-defined arguments following the mandatory two arguments shown above.
Simply define a lambda function when calling `master.foreach` as follows.

~~~~{.cpp}
void foo(Block* b,                             // local block
         const diy::Master::ProxyWithLink& cp, // communication proxy for neighbor blocks
         int extra_arg);                       // user-defined additional argument(s)

int main(int argc, char** argv)
{
    ...

    int extra_arg = ...;
    master.foreach([&](Block* b, const diy::Master::ProxyWithLink& cp)
                { foo(b, cp, extra_arg); });
    ...
}
~~~~

If the callback is a member function of `Block`, then it can be called directly through the block without passing a pointer to
the block as an argument:

~~~~{.cpp}
struct Block
{
    ...

    void bar(const diy::Master::ProxyWithLink& cp, // communication proxy for neighbor blocks
             int extra_arg)                        // user-defined additional argument(s)
    {
        ...
    }
};

int main(int argc, char** argv)
{
    ...

    int extra_arg = ...;
    master.foreach([&](Block* b, const diy::Master::ProxyWithLink& cp)
                { b->bar(cp, extra_arg); });
    ...
}
~~~~
