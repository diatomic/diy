// Exercises various decomposition options and also is a good tutorial for creating blocks
// and initializing diy
// This example uses the "short" form of blocks without an AddBlock functor

#include <vector>
#include <iostream>
#include <bitset>

#include <diy/decomposition.hpp>
#include <diy/assigner.hpp>
#include <diy/master.hpp>

typedef     diy::DiscreteBounds         Bounds;
typedef     diy::RegularGridLink        RGLink;

// the block structure
struct Block
{
    // following is mandatory
    Block()                             {}
    static void*    create()            { return new Block; }
    static void     destroy(void* b)    { delete static_cast<Block*>(b); }

    // the rest is optional
    void show_link(const diy::Master::ProxyWithLink& cp)
        {
            diy::RegularLink<Bounds>* link = static_cast<diy::RegularLink<Bounds>*>(cp.link());
            std::cout << "Block (" << cp.gid() << "): "
                      << link->core().min[0]   << ' ' << link->core().min[1]   << ' '
                      << link->core().min[2] << " - "
                      << link->core().max[0]   << ' ' << link->core().max[1]   << ' '
                      << link->core().max[2] << " : "
                      << link->bounds().min[0] << ' ' << link->bounds().min[1] << ' '
                      << link->bounds().min[2] << " - "
                      << link->bounds().max[0] << ' ' << link->bounds().max[1] << ' '
                      << link->bounds().max[2] << " : "
                      << link->size()   << ' ' //<< std::endl
                      << std::dec
                      << std::endl;
        }
};

int main(int argc, char* argv[])
{
    diy::mpi::environment     env(argc, argv);         // diy equivalent of MPI_Init
    diy::mpi::communicator    world;                   // diy equivalent of MPI communicator

    int                       size    = 8;             // total number of MPI processes
    int                       nblocks = 32;            // total number of blocks in global domain
    diy::ContiguousAssigner   assigner(size, nblocks);

    Bounds domain(3);                                   // global data size
    domain.min[0] = domain.min[1] = domain.min[2] = 0;
    domain.max[0] = domain.max[1] = domain.max[2] = 255;

    int rank = world.rank();                           // MPI rank of this process
    std::cout << "Rank " << rank << ":" << std::endl;
    diy::Master master(world,
                       1,                              // one thread
                       -1,                             // all blocks in memory
                       &Block::create,
                       &Block::destroy);

    // share_face is an n-dim (size 3 in this example) vector of bools
    // indicating whether faces are shared in each dimension
    // uninitialized values default to false
    diy::RegularDecomposer<Bounds>::BoolVector          share_face;
    share_face.push_back(true);

    // wrap is an n-dim (size 3 in this example) vector of bools
    // indicating whether boundary conditions are periodic in each dimension
    // uninitialized values default to false
    diy::RegularDecomposer<Bounds>::BoolVector          wrap;
    wrap.push_back(true);
    wrap.push_back(true);

    // ghosts is an n-dim (size 3 in this example) vector of ints
    // indicating number of ghost cells per side in each dimension
    // uninitialized values default to 0
    diy::RegularDecomposer<Bounds>::CoordinateVector    ghosts;
    ghosts.push_back(1); ghosts.push_back(2);

    // either create the regular decomposer and call its decompose function
    // (having the decomposer available is useful for its other member functions
    diy::RegularDecomposer<Bounds> decomposer(3,
                                              domain,
                                              nblocks,
                                              share_face,
                                              wrap,
                                              ghosts);
    decomposer.decompose(rank, assigner, master);

    // display the decomposition
    master.foreach(&Block::show_link);
}
