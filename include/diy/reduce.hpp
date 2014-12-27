#ifndef DIY_REDUCE_HPP
#define DIY_REDUCE_HPP

#include <vector>
#include "master.hpp"
#include "assigner.hpp"

namespace diy
{

struct ReduceProxy: public Communicator::Proxy
{
    typedef     std::vector<int>                            GIDVector;

                ReduceProxy(const Communicator::Proxy&      proxy,
                            void*                           block,
                            unsigned                        round,
                            const Assigner&                 assigner,
                            const GIDVector&                incoming_gids,
                            const GIDVector&                outgoing_gids):
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

      // advanced
      void          set_round(unsigned r)                   { round_ = r; }

    private:
      void*         block_;
      unsigned      round_;

      Link          in_link_;
      Link          out_link_;
};

namespace detail
{
  template<class Reduce, class Partners>
  struct ReductionFunctor;

  template<class Partners, class Skip>
  struct SkipInactiveOr;
}

/**
 * \ingroup Communication
 * \brief Implementation of the reduce communication pattern (includes
 *        swap-reduce, merge-reduce, and any other global communication).
 *
 * \TODO Detailed explanation.
 */
template<class Reduce, class Partners, class Skip>
void reduce(Master&                    master,
            const Assigner&            assigner,
            const Partners&            partners,
            const Reduce&              reduce,
            const Skip&                skip)
{
  int original_expected = master.communicator().expected();

  unsigned round;
  for (round = 0; round < partners.rounds(); ++round)
  {
    //fprintf(stderr, "== Round %d\n", round);
    master.foreach(detail::ReductionFunctor<Reduce,Partners>(round, reduce, partners, assigner),
                   detail::SkipInactiveOr<Partners,Skip>(round, partners, skip));

    int expected = 0;
    for (int i = 0; i < master.size(); ++i)
    {
      if (partners.active(round + 1, master.gid(i)))
      {
        std::vector<int> incoming_gids;
        partners.incoming(round + 1, master.gid(i), incoming_gids);
        expected += incoming_gids.size();
        master.communicator().incoming(master.gid(i)).clear();
      }
    }
    master.communicator().set_expected(expected);
    master.communicator().flush();
  }
  // final round
  //fprintf(stderr, "== Round %d\n", round);
  master.foreach(detail::ReductionFunctor<Reduce,Partners>(round, reduce, partners, assigner),
                 detail::SkipInactiveOr<Partners,Skip>(round, partners, skip));

  master.communicator().set_expected(original_expected);
}

template<class Reduce, class Partners>
void reduce(Master&                    master,
            const Assigner&            assigner,
            const Partners&            partners,
            const Reduce&              reducer)
{
  reduce(master, assigner, partners, reducer, Master::NeverSkip());
}

namespace detail
{
  template<class Reduce, class Partners>
  struct ReductionFunctor
  {
                ReductionFunctor(unsigned round_, const Reduce& reduce_, const Partners& partners_, const Assigner& assigner_):
                    round(round_), reduce(reduce_), partners(partners_), assigner(assigner_)        {}

    void        operator()(void* b, const Master::ProxyWithLink& cp, void*) const
    {
      if (!partners.active(round, cp.gid())) return;

      std::vector<int> incoming_gids, outgoing_gids;
      if (round > 0)
          partners.incoming(round, cp.gid(), incoming_gids);        // receive from the previous round
      if (round < partners.rounds())
          partners.outgoing(round, cp.gid(), outgoing_gids);        // send to the next round

      ReduceProxy   rp(cp, b, round, assigner, incoming_gids, outgoing_gids);
      reduce(b, rp, partners);

      // touch the outgoing queues to make sure they exist
      Communicator::OutgoingQueues& outgoing = const_cast<Communicator&>(cp.comm()).outgoing(cp.gid());
      if (outgoing.size() < rp.out_link().size())
        for (unsigned j = 0; j < rp.out_link().size(); ++j)
          outgoing[rp.out_link().target(j)];       // touch the outgoing queue, creating it if necessary
    }

    unsigned        round;
    const Reduce&   reduce;
    const Partners& partners;
    const Assigner& assigner;
  };

  template<class Partners, class Skip>
  struct SkipInactiveOr
  {
                    SkipInactiveOr(int round_, const Partners& partners_, const Skip& skip_):
                        round(round_), partners(partners_), skip(skip_)         {}
    bool            operator()(int i, const Master& master) const               { return !partners.active(round, master.gid(i)) || skip(i, master); }
    int             round;
    const Partners& partners;
    const Skip&     skip;
  };
}

} // diy

#endif // DIY_REDUCE_HPP
