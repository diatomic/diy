#ifndef DIY_REDUCE_OPERATIONS_HPP
#define DIY_REDUCE_OPERATIONS_HPP

#include "reduce.hpp"
#include "partners/swap.hpp"

namespace diy
{

namespace detail
{
  template<class Op>
  struct AllToAllReduce
  {
         AllToAllReduce(const Op& op_, const Assigner& assigner):
             op(op_)
    {
      for (int gid = 0; gid < assigner.nblocks(); ++gid)
      {
        BlockID nbr = { gid, assigner.rank(gid) };
        all_neighbors_link.add_neighbor(nbr);
      }
    }

    void operator()(void* b, const ReduceProxy& srp, const RegularSwapPartners& partners) const
    {
      if (srp.in_link().size() == 0)                // initial round
      {

        ReduceProxy all_srp(srp, srp.block(), 0, empty_link, all_neighbors_link);
        op(b, all_srp);

        Master::OutgoingQueues all_queues;
        all_queues.swap(*all_srp.outgoing());       // clears out the queues and stores them locally

        // enqueue outgoing
        int k_out = srp.out_link().size();
        int group = all_srp.out_link().size() / k_out;
        for (int i = 0; i < k_out; ++i)
        {
          std::pair<int,int> range(i*group, (i+1)*group);
          srp.enqueue(srp.out_link().target(i), range);
          for (int j = i*group; j < (i+1)*group; ++j)
          {
            int from = srp.gid();
            int to   = all_srp.out_link().target(j).gid;
            srp.enqueue(srp.out_link().target(i), std::make_pair(from, to));
            srp.enqueue(srp.out_link().target(i), all_queues[all_srp.out_link().target(j)]);
          }
        }
      } else if (srp.out_link().size() == 0)        // final round
      {
        // dequeue incoming + reorder into the correct order
        ReduceProxy all_srp(srp, srp.block(), 1, all_neighbors_link, empty_link);

        Master::IncomingQueues all_incoming;
        all_incoming.swap(*srp.incoming());

        int k_in  = srp.in_link().size();

        std::pair<int, int> range;      // all the ranges should be the same
        for (int i = 0; i < k_in; ++i)
        {
          int gid_in = srp.in_link().target(i).gid;
          MemoryBuffer& in = all_incoming[gid_in];
          load(in, range);
          while(in)
          {
            std::pair<int, int> from_to;
            load(in, from_to);
            load(in, all_srp.incoming(from_to.first));
          }
        }

        op(b, all_srp);
      } else                                        // intermediate round: reshuffle queues
      {
        int k_in  = srp.in_link().size();
        int k_out = srp.out_link().size();

        // add up buffer sizes
        std::vector<size_t> sizes_out(k_out, sizeof(std::pair<int,int>));
        std::pair<int, int> range;      // all the ranges should be the same
        for (int i = 0; i < k_in; ++i)
        {
          MemoryBuffer& in = srp.incoming(srp.in_link().target(i).gid);

          load(in, range);
          int group = (range.second - range.first)/k_out;

          std::pair<int, int> from_to;
          size_t s;
          while(in)
          {
            diy::load(in, from_to);
            diy::load(in, s);

            int j = (from_to.second - range.first) / group;
            sizes_out[j] += s + sizeof(size_t) + sizeof(std::pair<int,int>);
            in.skip(s);
          }
          in.reset();
        }

        // reserve outgoing buffers of correct size
        int group = (range.second - range.first)/k_out;
        for (int i = 0; i < k_out; ++i)
        {
          MemoryBuffer& out = srp.outgoing(srp.out_link().target(i));
          out.reserve(sizes_out[i]);

          std::pair<int, int> out_range;
          out_range.first  = range.first + group*i;
          out_range.second = range.first + group*(i+1);
          save(out, out_range);
        }

        // re-direct the queues
        for (int i = 0; i < k_in; ++i)
        {
          MemoryBuffer& in = srp.incoming(srp.in_link().target(i).gid);

          std::pair<int, int> range;
          load(in, range);

          std::pair<int, int> from_to;
          while(in)
          {
            load(in, from_to);
            int j = (from_to.second - range.first) / group;

            MemoryBuffer& out = srp.outgoing(srp.out_link().target(j));
            save(out, from_to);
            MemoryBuffer::copy(in, out);
          }
        }
      }
    }

    const Op&           op;
    Link                all_neighbors_link, empty_link;
  };
}

template<class Op>
void
all_to_all(Master&              master,     //!< block owner
           const Assigner&      assigner,   //!< global block locator (maps gid to proc)
           const Op&            op,         //!< user-defined operation called to enqueue and dequeue items
           int                  k = 2       //!< reduction fanout
          )
{
  RegularSwapPartners  partners(1, assigner.nblocks(), k, false);
  reduce(master, assigner, partners, detail::AllToAllReduce<Op>(op, assigner));
}

}

#endif
