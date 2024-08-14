Sometimes during the course of local communication, it's necessary to send or receive messages outside of the local
neighborhood. For example in particle tracing, a local algorithm that exchanges particles as they cross neighboring
block boundaries, the block containing a particle's final destination may need to send summary statistics carried along
by the particle (such as total number of hops) back to the originating block where the particle began its journey. For
such cases, there is a remote version of `diy::Master::exchange()`.

## Synchronous remote `rexchange`

Everything follows the synchronous `exchange` protocol of the [Local communication](local_comm.md) page, except that a
`remote` flag set to `true` is passed to `master.exchange`, and the `master.foreach()` callback functions can access
blocks outside of their neighborhood.
When enqueuing data remotely, DIY doesn't know the assignment of block global ID (`gid`) to MPI process rank; this
information is only kept for the local link. Hence, the full `diy::BlockID` information (a tuple of block gid and MPI
process rank) must be provided by the user. For dequeuing, we iterate over a vector of incoming block gids
extracted from the communication proxy, and then dequeue each of those messages.

~~~~{.cpp}
void foo(Block* b,                             // local block
         const diy::Master::ProxyWithLink& cp) // communication proxy for neighbor blocks
{
    diy::Link*    l = cp.link();                     // link to the neighbor blocks

    // compute some local value
    ...

    // for all neighbor blocks, enqueue data going to this neighbor block in the next exchange
    for (int i = 0; i < l->size(); ++i)
        cp.enqueue(l->target(i), value);

    // enqueue something remote outside of the neighborhood
    int dest_gid = ...;                         // block global ID of destination
    int dest_proc = ...;                        // MPI process of destination block
    diy::BlockID dest_block = {dest_gid, dest_proc};
    cp.enqueue(dest_block, value);
}

void bar(Block* b,                             // local block
         const diy::Master::ProxyWithLink& cp) // communication proxy for neighbor blocks
{
    std::vector<int> incoming_gids;
    cp.incoming(incoming_gids);

    // for anything incoming, dequeue data received in the last exchange
    for (int i = 0; i < incoming_gids.size(); i++)
    {
        int gid = incoming_gids[i];
        if (cp.incoming(gid).size())
        {
            int v;
            cp.dequeue(gid, v);
        }
    }

    // compute some local value
    ...
}

int main(int argc, char**argv)
{
  ...

  bool remote = true;
  master.foreach(&foo);
  master.exchange(remote);
  master.foreach(&bar);
}
~~~~

