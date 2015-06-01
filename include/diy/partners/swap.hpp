#ifndef DIY_PARTNERS_SWAP_HPP
#define DIY_PARTNERS_SWAP_HPP

#include "common.hpp"

namespace diy
{

class Master;

struct RegularSwapPartners: public RegularPartners
{
  typedef       RegularPartners                                 Parent;

                // contiguous parameter indicates whether to match partners contiguously or in a round-robin fashion;
                // contiguous is useful when data needs to be united;
                // round-robin is useful for vector-"halving"
                RegularSwapPartners(int dim, int nblocks, int k, bool contiguous = true):
                    Parent(dim, nblocks, k, contiguous)         {}
                RegularSwapPartners(const DivisionVector&   divs,
                                    const KVSVector&        kvs,
                                    bool  contiguous = true):
                    Parent(divs, kvs, contiguous)               {}

  bool          active(int round, int gid, const Master&) const                                 { return true; }    // in swap-reduce every block is always active

  void          incoming(int round, int gid, std::vector<int>& partners, const Master&) const   { Parent::fill(round - 1, gid, partners); }
  void          outgoing(int round, int gid, std::vector<int>& partners, const Master&) const   { Parent::fill(round, gid, partners); }
};

} // diy

#endif
