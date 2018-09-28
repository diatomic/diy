#include <diy/mpi.hpp>
#include <diy/io/shared.hpp>

int main(int argc, char** argv)
{
    diy::mpi::environment   env(argc, argv);
    diy::mpi::communicator  world;

    diy::io::SharedOutFile out("shared-output.txt", world);

    out << world.rank() << " out of " << world.size() << std::endl;
}
