#ifndef DIY_SWAP_REDUCE_HPP
#define DIY_SWAP_REDUCE_HPP

#include <vector>
#include "../master.hpp"
#include "../assigner.hpp"
#include "../decomposition.hpp"
#include "../types.hpp"

// TODO: create KDTreePartners

namespace diy
{

struct RegularPartners
{
  // The record of group size per round in a dimension
  struct DimK
  {
            DimK(int dim_, int round_, int k_):
                dim(dim_), round(round_), size(k_)               {}

    int dim;
    int round;          // round in this dimension
    int size;           // group size
  };

  // The part of RegularDecomposer that we need works the same with either Bounds (so we fix them arbitrarily)
  typedef       DiscreteBounds                      Bounds;
  typedef       RegularDecomposer<Bounds>           Decomposer;

  typedef       std::vector<int>                    CoordVector;
  typedef       std::vector<int>                    DivisionVector;
  typedef       std::vector<DimK>                   KVSVector;

                // contiguous parameter indicates whether to match partners contiguously or in a round-robin fashion;
                // contiguous is useful when data needs to be united;
                // round-robin is useful for vector-"halving"
                RegularPartners(int dim, int nblocks, int k, bool contiguous = true):
                  divisions_(dim, 0),
                  contiguous_(contiguous)                       { Decomposer::fill_divisions(dim, nblocks, divisions_); factor(k, divisions_, kvs_); }
                RegularPartners(const DivisionVector&   divs,
                                const KVSVector&        kvs,
                                bool  contiguous = true):
                  divisions_(divs), kvs_(kvs),
                  contiguous_(contiguous)                       {}

  size_t        rounds() const                                  { return kvs_.size(); }
  int           size(int round) const                           { return kvs_[round].size; }
  int           dim(int round) const                            { return kvs_[round].dim; }

  static
  inline void   factor(int k, const DivisionVector& divisions, KVSVector& kvs);

  inline void   fill(int round, int gid, std::vector<int>& partners) const;
  inline int    group_position(int round, int c, int step) const;

  private:
    static void factor(int k, int tot_b, std::vector<int>& kvs);

    DivisionVector      divisions_;
    KVSVector           kvs_;
    bool                contiguous_;
};


struct SwapReduceProxy: public Communicator::Proxy
{
    typedef     std::vector<int>                            GIDVector;

                SwapReduceProxy(const Communicator::Proxy&  proxy,
                                void*                       block,
                                unsigned                    round,
                                const Assigner&             assigner,
                                const GIDVector&            incoming_gids,
                                const GIDVector&            outgoing_gids):
                    Communicator::Proxy(proxy),
                    block_(block),
                    round_(round)
    {
      // setup in_link
      for (unsigned i = 0; i < incoming_gids.size(); ++i)
      {
        BlockID nbr;
        nbr.gid  = incoming_gids[i];
        nbr.proc = assigner.rank(nbr.gid);
        in_link_.add_neighbor(nbr);
      }

      // setup out_link
      for (unsigned i = 0; i < outgoing_gids.size(); ++i)
      {
        BlockID nbr;
        nbr.gid  = outgoing_gids[i];
        nbr.proc = assigner.rank(nbr.gid);
        out_link_.add_neighbor(nbr);
      }
    }

      void*         block() const                           { return block_; }
      unsigned      round() const                           { return round_; }

      const Link&   in_link() const                         { return in_link_; }
      const Link&   out_link() const                        { return out_link_; }

    private:
      void*         block_;
      unsigned      round_;

      Link          in_link_;
      Link          out_link_;
};

namespace detail
{
  template<class Reduce, class Partners>
  struct ReductionProxy;
}

template<class Reduce, class Partners>
void swap_reduce(Master&                    master,
                 const Assigner&            assigner,
                 const Partners&            partners,
                 const Reduce&              reduce)
{
  int original_expected = master.communicator().expected();

  unsigned round;
  for (round = 0; round < partners.rounds(); ++round)
  {
    //fprintf(stderr, "== Round %d\n", round);
    master.foreach(detail::ReductionProxy<Reduce,Partners>(round, reduce, partners, assigner));

    int expected = master.size() * partners.size(round);
    master.communicator().set_expected(expected);
    for (unsigned i = 0; i < master.size(); ++i)
      master.communicator().incoming(master.gid(i)).clear();
    master.communicator().flush();
  }
  //fprintf(stderr, "== Round %d\n", round);
  master.foreach(detail::ReductionProxy<Reduce,Partners>(round, reduce, partners, assigner));     // final round

  master.communicator().set_expected(original_expected);
}

namespace detail
{
  template<class Reduce, class Partners>
  struct ReductionProxy
  {
                ReductionProxy(unsigned round_, const Reduce& reduce_, const Partners& partners_, const Assigner& assigner_):
                    round(round_), reduce(reduce_), partners(partners_), assigner(assigner_)        {}

    void        operator()(void* b, const Master::ProxyWithLink& cp, void*) const
    {
      std::vector<int> incoming_gids, outgoing_gids;
      if (round > 0)
          partners.fill(round - 1, cp.gid(), incoming_gids);        // receive from the previous round
      if (round < partners.rounds())
          partners.fill(round, cp.gid(), outgoing_gids);            // send to the next round

      SwapReduceProxy   srp(cp, b, round, assigner, incoming_gids, outgoing_gids);
      reduce(b, srp, partners);

      // touch the outgoing queues to make sure they exist
      Communicator::OutgoingQueues& outgoing = const_cast<Communicator&>(cp.comm()).outgoing(cp.gid());
      if (outgoing.size() < srp.out_link().count())
        for (unsigned j = 0; j < srp.out_link().count(); ++j)
          outgoing[srp.out_link().target(j)];       // touch the outgoing queue, creating it if necessary
    }

    unsigned        round;
    const Reduce&   reduce;
    const Partners& partners;
    const Assigner& assigner;
  };
}

}

void
diy::RegularPartners::
fill(int round, int gid, std::vector<int>& partners) const
{
  const DimK&   kv  = kvs_[round];
  partners.reserve(kv.size - 1);

  int step; // gids jump by this much in the current round
  if (contiguous_)
  {
    step = 1;

    for (int r = 0; r < round; ++r)
    {
      if (kvs_[r].dim != kv.dim)
        continue;
      step *= kvs_[r].size;
    }
  } else
  {
    step = divisions_[kv.dim];
    for (int r = 0; r < round + 1; ++r)
    {
      if (kvs_[r].dim != kv.dim)
        continue;
      step /= kvs_[r].size;
    }
  }

  CoordVector   coords;
  Decomposer::gid_to_coords(gid, coords, divisions_);
  int c   = coords[kv.dim];
  int pos = group_position(round, c, step);

  int partner = c - pos * step;
  coords[kv.dim] = partner;
  int partner_gid = Decomposer::coords_to_gid(coords, divisions_);
  partners.push_back(partner_gid);

  for (int k = 1; k < kv.size; ++k)
  {
    partner += step;
    coords[kv.dim] = partner;
    int partner_gid = Decomposer::coords_to_gid(coords, divisions_);
    partners.push_back(partner_gid);
  }
}

// Tom's GetGrpPos
int
diy::RegularPartners::
group_position(int round, int c, int step) const
{
  // the second term in the following expression does not simplify to
  // (gid - start_b) / kv[r]
  // because the division gid / (step * kv[r]) is integer and truncates
  // this is exactly what we want
  int g = c % step + c / (step * kvs_[round].size) * step;
  int p = c / step % kvs_[round].size;
  static_cast<void>(g);        // shut up the compiler

  // g: group number (output)
  // p: position number within the group (output)
  return p;
}

void
diy::RegularPartners::
factor(int k, const DivisionVector& divisions, KVSVector& kvs)
{
  // factor in each dimension
  std::vector< std::vector<int> >       tmp_kvs(divisions.size());
  for (unsigned i = 0; i < divisions.size(); ++i)
    factor(k, divisions[i], tmp_kvs[i]);

  // interleave the dimensions
  int round = 0;
  std::vector<int>  round_per_dim(divisions.size(), 0);
  while(true)
  {
    // TODO: not the most efficient way to do this
    bool changed = false;
    for (unsigned i = 0; i < divisions.size(); ++i)
    {
      if (round_per_dim[i] == tmp_kvs[i].size())
        continue;
      kvs.push_back(DimK(i, round++, tmp_kvs[i][round_per_dim[i]++]));
      changed = true;
    }
    if (!changed)
        break;
  }
}

// Tom's FactorK
void
diy::RegularPartners::
factor(int k, int tot_b, std::vector<int>& kv)
{
  int rem = tot_b; // unfactored remaining portion of tot_b
  int j;

  while (rem > 1)
  {
    // remainder is divisible by k
    if (rem % k == 0)
    {
      kv.push_back(k);
      rem /= k;
    }
    // if not, start at k and linearly look for smaller factors down to 2
    else
    {
      for (j = k - 1; j > 1; j--)
      {
        if (rem % j == 0)
        {
          kv.push_back(j);
          rem /= k;
          break;
        }
      }
      if (j == 1)
      {
        kv.push_back(rem);
        rem = 1;
      }
    } // else
  } // while
}

#endif // DIY_SWAP_REDUCE_HPP
