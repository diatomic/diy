\defgroup Decomposition

Provides regular decomposition of the domain.

Example     {#decomposition-example}
-------

~~~{.cpp}

// callback function signature
void create(int gid,
            const Bounds& core,
            const Bounds& bounds,
            const Bounds& domain,
            const diy::Link& link);

diy::Master                                         master(...);

// share_face is a vector of bools indicating whether faces are shared in each dimension
// uninitialized values default to false
diy::RegularDecomposer<Bounds>::BoolVector          share_face;

// wrap is a vector of bools indicating whether boundary conditions are periodic in each dimension
// uninitialized values default to false
diy::RegularDecomposer<Bounds>::BoolVector          wrap;

// ghosts is a vector of ints indicating number of ghost cells per side in each dimension
// uninitialized values default to 0
diy::RegularDecomposer<Bounds>::CoordinateVector    ghosts;


// --- various ways to decompose a domain follow ---

// create a RegularDecomposer
// allows access to all the methods in RegularDecomposer
diy::RegularDecomposer<Bounds> decomposer(dim,
                                          domain,
                                          nblocks,
                                          share_face,       // optional
                                          wrap,             // optional
                                          ghosts);          // optional

// --- and ---

// call the decomposer's decompose function given
// a create function of the signature above
decomposer.decompose(world.rank(),
                     assigner,
                     create);

// --- or ---

// call the decomposer's decompose function given a lambda
decomposer.decompose(world.rank(),
                     assigner,
                     [&](int gid,                   // block global id
                         const Bounds& core,        // block bounds without any ghost added
                         const Bounds& bounds,      // block bounds including any ghost region added
                         const Bounds& domain,      // global data bounds
                         const RCLink& link)        // neighborhood
                     {
                         Block*          b   = new Block;             // possibly use custom initialization
                         RGLink*         l   = new RGLink(link);
                         int             lid = master.add(gid, b, l); // add block to the master (mandatory)

                         // process any additional args here, load the data, etc.
                     });

// --- or ---

// call the decomposer's decompose function given master only
// (add the block to master and do nothing else)
decomposer.decompose(world.rank(),
                     assigner,
                     master);

~~~
