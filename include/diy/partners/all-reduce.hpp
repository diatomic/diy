#ifndef DIY_PARTNERS_ALL_REDUCE_HPP
#define DIY_PARTNERS_ALL_REDUCE_HPP

#include "merge.hpp"

namespace diy
{

// Follow merge reduction up and down

struct RegularAllReducePartners: public RegularMergePartners
{
  typedef       RegularMergePartners                            Parent;

                // contiguous parameter indicates whether to match partners contiguously or in a round-robin fashion;
                // contiguous is useful when data needs to be united;
                // round-robin is useful for vector-"halving"
                RegularAllReducePartners(int dim, int nblocks, int k, bool contiguous = true):
                    Parent(dim, nblocks, k, contiguous)         {}
                RegularAllReducePartners(const DivisionVector&   divs,
                                         const KVSVector&        kvs,
                                         bool  contiguous = true):
                    Parent(divs, kvs, contiguous)               {}

  size_t        rounds() const                                  { return 2*Parent::rounds(); }
  int           size(int round) const                           { return Parent::size(parent_round(round)); }
  int           dim(int round) const                            { return Parent::dim(parent_round(round)); }
  inline bool   active(int round, int gid) const                { return Parent::active(parent_round(round), gid); }

  int           parent_round(int round) const                   { return round < Parent::rounds() ? round : rounds() - round; }

  // incoming is only valid for an active gid; it will only be called with an active gid
  inline void   incoming(int round, int gid, std::vector<int>& partners) const
  {
      if (round <= Parent::rounds())
          Parent::incoming(round, gid, partners);
      else
          Parent::outgoing(parent_round(round), gid, partners);
  }

  inline void   outgoing(int round, int gid, std::vector<int>& partners) const
  {
      if (round < Parent::rounds())
          Parent::outgoing(round, gid, partners);
      else
          Parent::incoming(parent_round(round), gid, partners);
  }
};

} // diy

#endif


