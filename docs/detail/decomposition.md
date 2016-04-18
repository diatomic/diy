\defgroup Decomposition

Provides regular decomposition of the domain.

Example     {#decomposition-example}
-------

~~~{.cpp}

// block create function or AddBlock functor with
// overloaded function call of the same signature
void create(int gid,
            const Bounds& core,
            const Bounds& bounds,
            const Bounds& domain,
            const diy::Link& link);

// --- or ---

// functor to add blocks to master
struct AddBlock
{
  void operator()(int gid,
                  const Bounds& core,
                  const Bounds& bounds,
                  const Bounds& domain,
                  const diy::Link& link)
   const
   {
     ...
   }
}                                                   create;

diy::Master(...)                                    master;

// share_face is a vector of bools indicating whether faces are shared in each dimension
// uninitialized values default to false
diy::RegularDecomposer<Bounds>::BoolVector          share_face;

// wrap is a vector of bools indicating whether boundary conditions are periodic in each dimension
// uninitialized values default to false
diy::RegularDecomposer<Bounds>::BoolVector          wrap;

// ghosts is a vector of ints indicating number of ghost cells per side in each dimension
// uninitialized values default to 0
diy::RegularDecomposer<Bounds>::CoordinateVector    ghosts;


// --- various ways to decompose a 3D domain follow (choose one) ---


// use a helper function given the AddBlock functor or a
// create function of the signature above
diy::decompose(dim,
               rank,
               domain,
               assigner,
               create,
               share_face,
               wrap,
               ghosts);

// --- or ---

// use a helper function given the master
// for the "short form" of creating blocks, w/o the functor or signature above
diy::decompose(dim,
               rank,
               domain,
               assigner,
               master,
               share_face,
               wrap,
               ghosts);

// --- or ---

// create a RegularDecomposer
// allows access to all the methods in RegularDecomposer
diy::RegularDecomposer<Bounds> decomposer(dim,
                                          domain,
                                          nblocks,
                                          share_face,
                                          wrap,
                                          ghosts);

// --- and ---

// call the decomposer's decompose function given AddBlock or
// a create function of the signature above
decomposer.decompose(world.rank(),
                     assigner,
                     create);

// --- or ---

// call the decomposer's decompose function given master only
// (uses the master's AddBlock functor instead)
decomposer.decompose(world.rank(),
                     assigner,
                     master);

~~~
