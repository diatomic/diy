The `diy::RegularDecomposer` is used to decompose a domain into a regular grid of blocks. The blocks are created, linked by nearest
neighbors, and added to the master inside of a user-defined callback function that is passed to the decomposer.
The domain and resulting block bounds can be either discrete (integer-valued) or continuous (floating-point).

Not all problems have regular domains and regular block decompositions. `Decomposer` is a helper object for regular
problems, but its use is optional. For irregular (e.g., graph) problems, the domain can be custom decomposed by the user
and blocks can be linked to neighbors manually.

## Callback function for Decomposer

A callback function passed to the decomposer creates blocks, links them together by nearest neighbors, and adds them to
the `Master` object. The signature for such a function and an example of its contents are below. This function can be
defined globally or as a lambda function. Two sets of block bounds, `core` and `bounds` are provided to the user: the
former exclude any ghost region (overlap between blocks) while the latter include any ghost region. In addition, the
overall global domain bounds are provided as a convenience in `domain`. We recommended that the user store some of
these bounds in the block for further reference, as they are the result of the decomposition and usually
extremely useful.

~~~{.cpp}
typedef     diy::RegularGridLink        RGLink;

void link(int gid,
          const Bounds& core,                   // the block bounds excluding any ghost region
          const Bounds& bounds,                 // the block bounds including any ghost region
          const Bounds& domain,                 // the global domain bounds
          const RGLink& link)                   // link to the neighboring blocks
{
    Block*          b   = new Block;             // create a new block, perform any custom initialization
    RGLink*         l   = new RGLink(link);      // copy the link so that master owns a copy
    int             lid = master.add(gid, b, l); // add block to the master (mandatory)

    // process any additional args here, save the bounds, etc.
}
~~~

## Constructing and using Decomposer

Various ways to construct and use the Decomposer are illustrated in the snippet below.

~~~{.cpp}
typedef     diy::RegularContinuousLink        RCLink;

diy::Master master(...);

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
diy::RegularDecomposer<Bounds> decomposer(dim,              // dimensionality of the domain
                                          domain,           // overall bounds of global domain
                                          nblocks,          // global number of blocks
                                          share_face,       // optional
                                          wrap,             // optional
                                          ghosts);          // optional

// --- and ---

// call the decomposer's decompose function given a callback function of the signature above
decomposer.decompose(world.rank(),                  // MPI rank of this process
                     assigner,                      // diy::Assigner object
                     link);                         // callback function for decomposer

// --- or ---

// call the decomposer's decompose function given a lambda
decomposer.decompose(world.rank(),                  // MPI rank of this process
                     assigner,                      // diy::Assigner object
                     [&](int gid,                   // block global id
                         const Bounds& core,        // block bounds without any ghost added
                         const Bounds& bounds,      // block bounds including any ghost region added
                         const Bounds& domain,      // global data bounds
                         const RCLink& link)        // link to neighboring blocks
                     {
                         Block*          b   = new Block;             // create a new block, perform any custom initialization
                         RGLink*         l   = new RCLink(link);      // copy the link provided so that master owns a copy
                         int             lid = master.add(gid, b, l); // add block to the master (mandatory)

                         // process any additional args here, save the bounds, etc.
                     });

// --- or ---

// call the decomposer's decompose function given master only
// (add the block to master and do nothing else)
decomposer.decompose(world.rank(),
                     assigner,
                     master);
~~~
