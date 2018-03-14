#ifndef DIY_ASSIGNER_HPP
#define DIY_ASSIGNER_HPP

#include <vector>

namespace diy
{
  // Derived types should define
  //   int rank(int gid) const
  // that converts a global block id to a rank that it's assigned to.
  class Assigner
  {
    public:
     /**
      * \ingroup Assignment
      * \brief Manages how blocks are assigned to processes
      */
                    Assigner(int size__,     //!< total number of processes
                             int nblocks__   //!< total (global) number of blocks
                             ):
                      size_(size__), nblocks_(nblocks__)  {}

      //! returns the total number of process ranks
      int           size() const                        { return size_; }
      //! returns the total number of global blocks
      int           nblocks() const                     { return nblocks_; }
      //! sets the total number of global blocks
      void          set_nblocks(int nblocks__)          { nblocks_ = nblocks__; }
      //! returns the process rank of the block with global id gid (need not be local)
      virtual int   rank(int gid) const     =0;

    private:
      int           size_;      // total number of ranks
      int           nblocks_;   // total number of blocks
  };

  class StaticAssigner: public Assigner
  {
    public:
     /**
      * \ingroup Assignment
      * \brief Intermediate type to express assignment that cannot change; adds `local_gids` query method
      */
      using Assigner::Assigner;

      //! gets the local gids for a given process rank
      virtual void  local_gids(int rank, std::vector<int>& gids) const   =0;
  };

  class ContiguousAssigner: public StaticAssigner
  {
    public:
     /**
      * \ingroup Assignment
      * \brief Assigns blocks to processes in contiguous gid (block global id) order
      */
      using StaticAssigner::StaticAssigner;

      using StaticAssigner::size;
      using StaticAssigner::nblocks;

      int   rank(int gid) const override
      {
          int div = nblocks() / size();
          int mod = nblocks() % size();
          int r = gid / (div + 1);
          if (r < mod)
          {
              return r;
          } else
          {
              return mod + (gid - (div + 1)*mod)/div;
          }
      }
      inline
      void  local_gids(int rank, std::vector<int>& gids) const override;
  };

  class RoundRobinAssigner: public StaticAssigner
  {
    public:
     /**
      * \ingroup Assignment
      * \brief Assigns blocks to processes in cyclic or round-robin gid (block global id) order
      */
      using StaticAssigner::StaticAssigner;

      using StaticAssigner::size;
      using StaticAssigner::nblocks;

      int   rank(int gid) const override        { return gid % size(); }
      inline
      void  local_gids(int rank, std::vector<int>& gids) const override;
  };
}

void
diy::ContiguousAssigner::
local_gids(int rank_, std::vector<int>& gids) const
{
  int div = nblocks() / size();
  int mod = nblocks() % size();

  int from, to;
  if (rank_ < mod)
      from = rank_ * (div + 1);
  else
      from = mod * (div + 1) + (rank_ - mod) * div;

  if (rank_ + 1 < mod)
      to = (rank_ + 1) * (div + 1);
  else
      to = mod * (div + 1) + (rank_ + 1 - mod) * div;

  for (int gid = from; gid < to; ++gid)
    gids.push_back(gid);
}

void
diy::RoundRobinAssigner::
local_gids(int rank_, std::vector<int>& gids) const
{
  int cur = rank_;
  while (cur < nblocks())
  {
    gids.push_back(cur);
    cur += size();
  }
}

#endif
