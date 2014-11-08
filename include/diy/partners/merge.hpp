#ifndef DIY_PARTNERS_MERGE_HPP
#define DIY_PARTNERS_MERGE_HPP

#include "common.hpp"

namespace diy
{

struct RegularMergePartners: public RegularPartners
{
  typedef       RegularPartners                                 Parent;

                // contiguous parameter indicates whether to match partners contiguously or in a round-robin fashion;
                // contiguous is useful when data needs to be united;
                // round-robin is useful for vector-"halving"
                RegularMergePartners(int dim, int nblocks, int k, bool contiguous = true):
                    Parent(dim, nblocks, k, contiguous)         {}
                RegularMergePartners(const DivisionVector&   divs,
                                     const KVSVector&        kvs,
                                     bool  contiguous = true):
                    Parent(divs, kvs, contiguous)               {}

  inline bool   active(int round, int gid) const;

  // incoming is only valid for an active gid; it will only be called with an active gid
  inline void   incoming(int round, int gid, std::vector<int>& partners) const    { Parent::fill(round - 1, gid, partners); }
  // this is a lazy implementation of outgoing, but it reuses the existing code
  inline void   outgoing(int round, int gid, std::vector<int>& partners) const    { std::vector<int> tmp; Parent::fill(round, gid, tmp); partners.push_back(tmp[0]); }
};

} // diy

bool
diy::RegularMergePartners::
active(int round, int gid) const
{
  if (round == rounds())
      return gid == 0;          // 0 is always the root

  CoordVector   coords;
  Decomposer::gid_to_coords(gid, coords, divisions());

  std::vector<int> steps;
  Parent::fill_steps(steps, round);

  for (int r = 0; r < round; ++r)
      if (Parent::group_position(r, coords[kvs()[r].dim], steps[r]) != 0)
          return false;

  return true;
}

#endif

