#ifndef DIY_COMMUNICATOR_HPP
#define DIY_COMMUNICATOR_HPP

#include <list>
#include <map>

#include "types.hpp"
#include "cover.hpp"
#include "mpi.hpp"
#include "serialization.hpp"
#include "detail/collectives.hpp"

namespace diy
{
  class Communicator
  {
    public:
      struct Proxy;                 // passed to the block: encapsulates information like block id
      struct InFlight;
      struct Collective;
      struct tags       { enum { queue }; };

      typedef           std::list<InFlight>                 InFlightList;
      typedef           std::list<Collective>               CollectivesList;
      typedef           std::map<int, CollectivesList>      CollectivesMap;     // gid          -> [collectives]


      // TODO: these types will have to be adjusted to support multi-threading
      typedef           std::map<int,     BinaryBuffer>     IncomingQueues;     //  gid         -> queue
      typedef           std::map<BlockID, BinaryBuffer>     OutgoingQueues;     // (gid, proc)  -> queue
      typedef           std::map<int,     IncomingQueues>   IncomingQueuesMap;  //  gid         -> {  gid       -> queue }
      typedef           std::map<int,     OutgoingQueues>   OutgoingQueuesMap;  //  gid         -> { (gid,proc) -> queue }

    public:
                        Communicator(mpi::communicator& comm):
                          comm_(comm),
                          expected_(0),
                          received_(0)                  {}

      int               rank() const                    { return comm_.rank(); }
      inline Proxy      proxy(int gid);

      IncomingQueues&   incoming(int gid)               { return incoming_[gid]; }
      OutgoingQueues&   outgoing(int gid)               { return outgoing_[gid]; }
      CollectivesList&  collectives(int gid)            { return collectives_[gid]; }

      void              set_expected(int expected)      { expected_ = expected; }
      void              add_expected(int i)             { expected_ += i; }

      inline void       exchange();     // possibly called in between block computations
      inline void       flush();        // makes sure all the serialized queues migrate to their target processors
      inline bool       nudge();
      inline void       process_collectives();

      void              cancel_requests();              // TODO

    private:
      mpi::communicator&    comm_;
      IncomingQueuesMap     incoming_;
      OutgoingQueuesMap     outgoing_;
      InFlightList          inflight_;
      CollectivesMap        collectives_;
      int                   expected_;
      int                   received_;
  };

  struct Communicator::Proxy
  {
                        Proxy(Communicator* comm, int gid):
                          gid_(gid),
                          comm_(comm),
                          incoming_(&comm_->incoming(gid)),
                          outgoing_(&comm_->outgoing(gid)),
                          collectives_(&comm_->collectives(gid))    {}

    int                 gid() const                                     { return gid_; }

    template<class T>
    void                enqueue(const BlockID& to, const T& x,
                                void (*save)(BinaryBuffer&, const T&) = &::diy::save<T>) const
    { OutgoingQueues& out = *outgoing_; save(out[to], x); }

    template<class T>
    void                dequeue(int from, T& x,
                                void (*load)(BinaryBuffer&, T&) = &::diy::load<T>) const
    { IncomingQueues& in  = *incoming_; load(in[from], x); }

    inline void         incoming(std::vector<int>& v) const;            // fill v with every gid from which we have a message

    template<class T, class Op>
    inline void         all_reduce(const T& in, std::ptrdiff_t out, Op op) const;

    private:
      Communicator*     comm_;
      int               gid_;
      IncomingQueues*   incoming_;
      OutgoingQueues*   outgoing_;
      CollectivesList*  collectives_;
  };

  struct Communicator::InFlight
  {
    BinaryBuffer        queue;
    mpi::request        request;

    // for debug purposes:
    int from;
    int to;
  };

  struct Communicator::Collective
  {
            Collective():
              cop_(0)                           {}
            Collective(std::ptrdiff_t out,
                       detail::CollectiveOp* cop):
              out_(out), cop_(cop)              {}
            // this copy constructor is very ugly, but need it to insert Collectives into a list
            Collective(const Collective& other):
              cop_(0)                           { swap(const_cast<Collective&>(other)); }
            ~Collective()                       { delete cop_; }

    void    swap(Collective& other)             { std::swap(cop_, other.cop_); std::swap(out_, other.out_); }
    void    update(const Collective& other)     { cop_->update(*other.cop_); }
    void    global(const mpi::communicator& c)  { cop_->global(c); }
    void    copy_from(Collective& other) const  { cop_->copy_from(*other.cop_); }
    void    result_out(void* block) const       { cop_->result_out(static_cast<char*>(block) + out_); }

    std::ptrdiff_t                              out_;
    detail::CollectiveOp*                       cop_;

    private:
    Collective& operator=(const Collective& other);
  };
}

void
diy::Communicator::
exchange()
{
  // TODO: currently isends to self; should probably optimize

  // isend outgoing queues
  for (OutgoingQueuesMap::iterator it = outgoing_.begin(); it != outgoing_.end(); ++it)
  {
    int from  = it->first;
    for (OutgoingQueues::iterator cur = it->second.begin(); cur != it->second.end(); ++cur)
    {
      int to   = cur->first.gid;
      int proc = cur->first.proc;

      inflight_.push_back(InFlight());
      inflight_.back().from = from;
      inflight_.back().to   = to;
      inflight_.back().queue.swap(cur->second);
      diy::save(inflight_.back().queue, std::make_pair(from, to));
      inflight_.back().request = comm_.isend(proc, tags::queue, inflight_.back().queue.buffer);
    }
  }
  outgoing_.clear();

  // kick requests
  while(nudge());

  // check incoming queues
  mpi::optional<mpi::status>        ostatus = comm_.iprobe(mpi::any_source, tags::queue);
  while(ostatus)
  {
    diy::BinaryBuffer bb;
    comm_.recv(ostatus->source(), tags::queue, bb.buffer);

    std::pair<int,int> from_to;
    diy::load_back(bb, from_to);
    int from = from_to.first;
    int to   = from_to.second;

    incoming_[to][from] = diy::BinaryBuffer();
    incoming_[to][from].swap(bb);
    ++received_;

    ostatus = comm_.iprobe(mpi::any_source, tags::queue);
  }
}

void
diy::Communicator::
flush()
{
  while (!inflight_.empty() || received_ < expected_)
    exchange();

  process_collectives();

  received_ = 0;
}

void
diy::Communicator::
process_collectives()
{
  if (collectives_.empty())
      return;

  typedef       CollectivesList::iterator       CollectivesIterator;
  std::vector<CollectivesIterator>  iters;
  std::vector<int>                  gids;
  for (CollectivesMap::iterator cur = collectives_.begin(); cur != collectives_.end(); ++cur)
  {
    gids.push_back(cur->first);
    iters.push_back(cur->second.begin());
  }

  while (iters[0] != collectives_.begin()->second.end())
  {
    for (unsigned j = 1; j < iters.size(); ++j)
    {
      // NB: this assumes that the operations are commutative
      iters[0]->update(*iters[j]);
    }
    iters[0]->global(comm_);        // do the mpi collective

    for (unsigned j = 1; j < iters.size(); ++j)
    {
      iters[j]->copy_from(*iters[0]);
      ++iters[j];
    }

    ++iters[0];
  }
}

bool
diy::Communicator::
nudge()
{
  bool success = false;
  for (InFlightList::iterator it = inflight_.begin(); it != inflight_.end(); ++it)
  {
    mpi::optional<mpi::status> ostatus = it->request.test();
    if (ostatus)
    {
      success = true;
      InFlightList::iterator rm = it;
      --it;
      inflight_.erase(rm);
    }
  }
  return success;
}

diy::Communicator::Proxy
diy::Communicator::
proxy(int gid)
{ return Proxy(this, gid); }

void
diy::Communicator::Proxy::
incoming(std::vector<int>& v) const
{
  for (IncomingQueues::const_iterator it = incoming_->begin(); it != incoming_->end(); ++it)
    v.push_back(it->first);
}

template<class T, class Op>
void
diy::Communicator::Proxy::
all_reduce(const T& in, std::ptrdiff_t out, Op op) const
{
  collectives_->push_back(Collective(out, new detail::AllReduceOp<T,Op>(in, op)));
}

#endif
