\defgroup Communication

DIY currently supports two communication patterns: neighborhood exchange and
global reduction. Additional lightweight collectives can be executed over the
communication proxy of the above patterns.

Neighborhood Exchange
---------------------

The basic building block of communication is the neighborhood. This is a local
collection of edges in a communication graph from the current block to other
blocks; information is exchanged over these edges. In DIY, the *communication
proxy* is the object that encapsulates the neighborhood. The communication
proxy includes the *link*, which is the collection of edges to the neighboring
blocks. This size of the link is the number of edges. In the communication
proxy, `link->target[i]` is the block connected to the i-th edge. Edges can be
added or removed dynamically. Data can be enqueued to and dequeued from any
target in the link as desired, and then all enqueued data are exchanged over
the link in one step.

Global Reduction
----------------

DIY supports general reductions to implement complex global operations over blocks.
These reductions are general-purpose (any
global communication pattern can be implemented); they operate over blocks,
which cycle in and out of core as necessary; the
operations are (multi-)threaded automatically using the same mechanism as
[foreach()](@ref diy::Master::foreach). Although any global communication can
be expressed using the reduction mechanism, all the reductions included in DIY
operate in rounds over a k-ary reduction tree. The value of k used in each round
can vary, but if it's fixed, the number of rounds is log_k(nblocks).

The following patterns are currently available.

- merge-reduce
    - create `diy::RegularMergePartners`
    - call `diy::reduce`
    - [see the example here](\ref reduce/merge-reduce.cpp)
- swap-reduce
    - create `diy::RegularSwapPartners`
    - call `diy::reduce`
    - [see the example here](\ref reduce/swap-reduce.cpp)
- all-reduce
    - create `diy::RegularAllReducePartners`
    - call `diy::reduce`
- all-to-all
    - call `diy:all_to_all`
    - [see the example here](\ref reduce/all-to-all.cpp)

Proxy Collectives
-----------------

When a lightweight reduction needs to be mixed into an existing pattern such as a neighborhood exchange, DIY has a mechanism for this. An example is a neighbor exchange that must iterate until the collective result indicates it is time to terminate (as in particle tracing in rounds until no block has any more work to do). The underlying mechanism works as follows.

- The input values from each block are pushed to a vector of inputs for the process.
- The corresponding MPI collective is called by the process.
- The reduced results are redistributed to the process' blocks.

The inputs are pushed by calling `all_reduce` from each block, and the outputs are popped by calling `get` from each block. The collective mechanism compares with the above reductions as follows:

- It is a simple member function of the communication proxy that is piggybacked onto the underlying proxy, not a completely new pattern using k-ary reductions (unlike above).
- The reduction works per block and can be out of core (as above) because it uses the underlying proxy.
- The operator is a simple predefined macro (unlike above). The following operators are available: `maximum<T>`, `minimum<T>`, `std::plus<T>`, `std::multiplies<T>`, `std::logical_and<T>`, and `std::logical_or<T>`.
- The result retrieved using the `get` function, not dequeue (unlike above)
- Currently, all_reduce is the only collective available.

A code snippet is below. The complete example is [here](\ref simple/simple.cpp)

~~~~{.cpp}

void foo(void* b_,                             // local block
         const diy::Master::ProxyWithLink& cp, // communication proxy
         void*)                                // user-defined additional arguments
{

  ...

  cp.all_reduce(value, std::plus<int>());      // local value being reduced
}

void bar(void* b_,                             // local block
         const diy::Master::ProxyWithLink& cp, // communication proxy
         void*)                                // user-defined additional arguments
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
