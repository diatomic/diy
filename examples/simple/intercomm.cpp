#include <vector>
#include <iostream>

#include <diy/mpi.hpp>
#include <diy/master.hpp>

#include "../opts.h"

struct ProducerBlock
{
};

struct ConsumerBlock
{
    ConsumerBlock(int n_producers)
    {
        for (int i = 0; i < 10; ++i)
            queries.emplace_back(rand() % n_producers, rand() % 100);
    }

    std::vector<std::pair<int,int>> queries;
    size_t cur  = 0;
};

int main(int argc, char* argv[])
{
    diy::mpi::environment   env(argc, argv); // equivalent of MPI_Init(argc, argv)/MPI_Finalize()
    diy::mpi::communicator  world;           // equivalent of MPI_COMM_WORLD

    srand(world.rank());

    bool producer = world.rank() < world.size() / 2;
    diy::mpi::communicator  local = world.split(producer);

    MPI_Comm intercomm;
    MPI_Intercomm_create(local, 0, world, /* remote_leader = */ producer ? world.size() / 2 : 0, /* tag = */ 0, &intercomm);

    diy::Master master(intercomm);
    if (producer)
    {
        master.add(local.rank(), new ProducerBlock, new diy::Link);

        // respond to queries
        master.iexchange([&local](ProducerBlock*, const diy::Master::ProxyWithLink& cp) -> bool
        {
            for (auto& x : *cp.incoming())
            {
                int   gid   = x.first;
                auto& queue = x.second;
                while (queue)
                {
                    int query;
                    diy::load(queue, query);

                    int response = query * local.rank();
                    cp.enqueue(diy::BlockID { gid, gid }, response);

                    fmt::print("[{} producer] received {} from {}; responded {}\n", local.rank(), query, gid, response);
                }
            }

            return true;       // done; producer never has any local work
        });
    } else
    {
        master.add(local.rank(), new ConsumerBlock(world.size()/2), new diy::Link);

        master.iexchange([&local](ConsumerBlock* b, const diy::Master::ProxyWithLink& cp) -> bool
        {
            // schedule 5 queries at a time
            for (int i = 0; i < 5 && b->cur < b->queries.size(); ++i, ++b->cur)
            {
                int target = b->queries[b->cur].first;
                int query  = b->queries[b->cur].second;
                cp.enqueue(diy::BlockID { target, target }, query);

                fmt::print("[{} consumer] Enqueueing {} to {}\n", local.rank(), query, target);
            }

            for (auto& x : *cp.incoming())
            {
                int   gid   = x.first;
                auto& queue = x.second;
                while (queue)
                {
                    int response;
                    diy::load(queue, response);
                    fmt::print("[{} consumer] Received {} from {}\n", local.rank(), response, gid);
                }
            }

            return b->cur == b->queries.size();
        });
    }
}
