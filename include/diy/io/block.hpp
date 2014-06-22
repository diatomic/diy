#ifndef DIY_IO_BLOCK_HPP
#define DIY_IO_BLOCK_HPP

#include <string>
#include <algorithm>

#include "../mpi.hpp"
#include "../assigner.hpp"
#include "../master.hpp"

// Read and write collections of blocks using MPI-IO
// TODO: save and restore the links
namespace diy
{
namespace io
{
  template<class Master>
  void
  write_blocks(const std::string&           outfilename,
               const mpi::communicator&     comm,
               Master&                      master,
               typename Master::SaveBlock   save = 0)
  {
    if (!save) save = master.saver();       // save is likely to be different from master.save()

    typedef mpi::io::offset     offset_t;

    unsigned size = master.size(),
             max_size, min_size;
    mpi::all_reduce(comm, size, max_size, mpi::maximum<unsigned>());
    mpi::all_reduce(comm, size, min_size, mpi::minimum<unsigned>());

    mpi::io::file f(comm, outfilename, mpi::io::file::wronly | mpi::io::file::create);

    offset_t  start = 0, shift;
    std::vector<offset_t>   offsets, counts;
    unsigned i;
    for (i = 0; i < max_size; ++i)
    {
      offset_t count = 0,
               offset;
      if (i < size)
      {
        // get the block from master and serialize it
        const void* block = master.get(i);
        BinaryBuffer bb;
        save(block, bb);
        count = bb.buffer.size();
        mpi::scan(comm, count, offset, std::plus<offset_t>());
        offset += start - count;
        mpi::all_reduce(comm, count, shift, std::plus<offset_t>());
        start += shift;

        if (i < min_size)       // up to min_size, we can do collective IO
          f.write_at_all(offset, bb.buffer);
        else
          f.write_at(offset, bb.buffer);
      }
      offsets.push_back(offset);
      counts.push_back(count);
    }

    if (comm.rank() == 0)
    {
      std::vector<offset_t> all_offsets, all_counts;
      mpi::gather(comm, offsets, all_offsets, 0);
      mpi::gather(comm, counts,  all_counts,  0);

      std::vector< std::pair<offset_t, offset_t> >  all_offset_counts;
      for (unsigned i = 0; i < all_offsets.size(); ++i)
        if (all_counts[i] != 0)
          all_offset_counts.push_back(std::make_pair(all_offsets[i], all_counts[i]));
      std::sort(all_offset_counts.begin(), all_offset_counts.end());

      unsigned footer_size = all_offset_counts.size();        // should be the same as master.size();
      BinaryBuffer bb;
      diy::save(bb, all_offset_counts);
      diy::save(bb, footer_size);

      offset_t footer_offset = all_offset_counts.back().first + all_offset_counts.back().second;
      f.write_at(footer_offset, bb.buffer);
    } else
    {
      mpi::gather(comm, offsets, 0);
      mpi::gather(comm, counts,  0);
    }
  }

  template<class Master>
  void
  read_blocks(const std::string&           outfilename,
              const mpi::communicator&     comm,
              Assigner&                    assigner,
              Master&                      master,
              typename Master::LoadBlock   load = 0)
  {
    if (!load) load = master.loader();      // load is likely to be different from master.load()

    typedef mpi::io::offset     offset_t;

    mpi::io::file f(comm, outfilename, mpi::io::file::rdonly);

    offset_t    footer_offset = f.size() - sizeof(unsigned);
    unsigned size;

    // Read the size
    f.read_at_all(footer_offset, (char*) &size, sizeof(size));

    // Read all_offset_counts
    footer_offset -= size*sizeof(std::pair<offset_t, offset_t>);
    std::vector< std::pair<offset_t, offset_t> >    all_offset_counts(size);
    f.read_at_all(footer_offset, all_offset_counts);

    // Get local gids from assigner
    assigner.set_nblocks(size);
    std::vector<int> gids;
    assigner.local_gids(comm.rank(), gids);

    // Read our blocks;
    // TODO: use collective IO, when possible
    for (unsigned i = 0; i < gids.size(); ++i)
    {
        offset_t offset = all_offset_counts[gids[i]].first,
                 count  = all_offset_counts[gids[i]].second;
        BinaryBuffer bb;
        bb.buffer.resize(count);
        f.read_at(offset, bb.buffer);
        void* b = master.create();
        load(b, bb);
        master.add(gids[i], b, new Link);
    }
  }
}
}

#endif
