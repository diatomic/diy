\defgroup Communication

DIY currently supports two communication patterns: neighborhood exchange and reduction.

Neighborhood Exchange
---------------------

The basic building block of communication is the neighborhood. This is a local collection of edges in a communication graph from the current block to other blocks; information is exchanged over these edges. In DIY, the *communication proxy* is the object that encapsulates the neighborhood. The communication proxy includes the *link*, which is the collection of edges to the neighboring blocks. This size of the link is the number of edges. In the communication proxy, *link->target[i]* is the block connected to the ith edge. Edges can be added or removed dynamically. Data can be enqueued to and dequeued from any target in the link as desired, and then all enqueued data are exchanged over the link in one step.

Reduction
-----------

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
