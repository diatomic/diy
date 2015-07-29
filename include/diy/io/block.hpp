#ifndef DIY_IO_BLOCK_HPP
#define DIY_IO_BLOCK_HPP

#include <string>
#include <algorithm>

#include <unistd.h>

#include "../mpi.hpp"
#include "../assigner.hpp"
#include "../master.hpp"

// Read and write collections of blocks using MPI-IO
namespace diy
{
namespace io
{
  namespace detail
  {
    typedef mpi::io::offset                 offset_t;

    #pragma pack(4)     // without this pragma the int gets padded and we lose exact binary match across saves
    struct GidOffsetCount
    {
                    GidOffsetCount():                                   // need to initialize a vector of given size
                        gid(-1), offset(0), count(0)                    {}

                    GidOffsetCount(int gid_, offset_t offset_, offset_t count_):
                        gid(gid_), offset(offset_), count(count_)       {}

        bool        operator<(const GidOffsetCount& other) const        { return gid < other.gid; }

        int         gid;
        offset_t    offset;
        offset_t    count;
    };
  }

  inline
  void
  write_blocks(const std::string&           outfilename,
               const mpi::communicator&     comm,
               Master&                      master,
               Master::SaveBlock            save = 0)
  {
    if (!save) save = master.saver();       // save is likely to be different from master.save()

    typedef detail::offset_t                offset_t;
    typedef detail::GidOffsetCount          GidOffsetCount;

    unsigned size = master.size(),
             max_size, min_size;
    mpi::all_reduce(comm, size, max_size, mpi::maximum<unsigned>());
    mpi::all_reduce(comm, size, min_size, mpi::minimum<unsigned>());

    // truncate the file
    if (comm.rank() == 0)
        truncate(outfilename.c_str(), 0);

    mpi::io::file f(comm, outfilename, mpi::io::file::wronly | mpi::io::file::create);

    offset_t  start = 0, shift;
    std::vector<GidOffsetCount>     offset_counts;
    unsigned i;
    for (i = 0; i < max_size; ++i)
    {
      offset_t count = 0,
               offset;
      if (i < size)
      {
        // get the block from master and serialize it
        const void* block = master.get(i);
        MemoryBuffer bb;
        LinkFactory::save(bb, master.link(i));
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

        offset_counts.push_back(GidOffsetCount(master.gid(i), offset, count));
      } else
      {
        // matching global operations
        mpi::scan(comm, count, offset, std::plus<offset_t>());
        mpi::all_reduce(comm, count, shift, std::plus<offset_t>());

        // -1 indicates that there is no block written here from this rank
        offset_counts.push_back(GidOffsetCount(-1, offset, count));
      }
    }

    if (comm.rank() == 0)
    {
      // round-about way of gather vector of vectors of GidOffsetCount to avoid registering a new mpi datatype
      std::vector< std::vector<char> > gathered_offset_count_buffers;
      MemoryBuffer oc_buffer; diy::save(oc_buffer, offset_counts);
      mpi::gather(comm, oc_buffer.buffer, gathered_offset_count_buffers, 0);

      std::vector<GidOffsetCount>  all_offset_counts;
      for (unsigned i = 0; i < gathered_offset_count_buffers.size(); ++i)
      {
        MemoryBuffer oc_buffer; oc_buffer.buffer.swap(gathered_offset_count_buffers[i]);
        std::vector<GidOffsetCount> offset_counts;
        diy::load(oc_buffer, offset_counts);
        for (unsigned j = 0; j < offset_counts.size(); ++j)
          if (offset_counts[j].gid != -1)
            all_offset_counts.push_back(offset_counts[j]);
      }
      std::sort(all_offset_counts.begin(), all_offset_counts.end());        // sorts by gid

      unsigned footer_size = all_offset_counts.size();        // should be the same as master.size();
      MemoryBuffer bb;
      diy::save(bb, all_offset_counts);
      diy::save(bb, footer_size);

      // find footer_offset as the max of (offset + count)
      offset_t footer_offset = 0;
      for (unsigned i = 0; i < all_offset_counts.size(); ++i)
      {
        offset_t end = all_offset_counts[i].offset + all_offset_counts[i].count;
        if (end > footer_offset)
            footer_offset = end;
      }
      f.write_at(footer_offset, bb.buffer);
    } else
    {
      MemoryBuffer oc_buffer; diy::save(oc_buffer, offset_counts);
      mpi::gather(comm, oc_buffer.buffer, 0);
    }
  }

  inline
  void
  read_blocks(const std::string&           infilename,
              const mpi::communicator&     comm,
              Assigner&                    assigner,
              Master&                      master,
              Master::LoadBlock            load = 0)
  {
    if (!load) load = master.loader();      // load is likely to be different from master.load()

    typedef detail::offset_t                offset_t;
    typedef detail::GidOffsetCount          GidOffsetCount;

    mpi::io::file f(comm, infilename, mpi::io::file::rdonly);

    offset_t    footer_offset = f.size() - sizeof(unsigned);
    unsigned size;

    // Read the size
    f.read_at_all(footer_offset, (char*) &size, sizeof(size));

    // Read all_offset_counts
    footer_offset -= size*sizeof(GidOffsetCount);
    std::vector<GidOffsetCount>  all_offset_counts(size);
    f.read_at_all(footer_offset, all_offset_counts);

    // Get local gids from assigner
    assigner.set_nblocks(size);
    std::vector<int> gids;
    assigner.local_gids(comm.rank(), gids);

    // Read our blocks;
    // TODO: use collective IO, when possible
    for (unsigned i = 0; i < gids.size(); ++i)
    {
        if (gids[i] != all_offset_counts[gids[i]].gid)
            fprintf(stderr, "Warning: gids don't match in diy::io::read_blocks(), %d vs %d\n", gids[i], all_offset_counts[gids[i]].gid);

        offset_t offset = all_offset_counts[gids[i]].offset,
                 count  = all_offset_counts[gids[i]].count;
        MemoryBuffer bb;
        bb.buffer.resize(count);
        f.read_at(offset, bb.buffer);
        Link* l = LinkFactory::load(bb);
        l->fix(assigner);
        void* b = master.create();
        load(b, bb);
        master.add(gids[i], b, l);
    }
  }
}
}

#endif
