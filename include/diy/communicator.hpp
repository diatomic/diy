#ifndef DIY_COMMUNICATOR_HPP
#define DIY_COMMUNICATOR_HPP

#include <list>
#include <map>

#include "types.hpp"
#include "cover.hpp"
#include "serialization.hpp"

namespace diy
{
  class Communicator
  {
    public:
      struct Proxy;                 // passed to the block: encapsulates information like block id
      struct InFlight;
      struct tags       { enum { queue }; };

      typedef           std::list<InFlight>                 InFlightList;

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

      void              set_expected(int expected)      { expected_ = expected; }
      void              add_expected(int i)             { expected_ += i; }

      inline void       exchange();     // possibly called in between block computations
      inline void       flush();        // makes sure all the serialized queues migrate to their target processors
      inline bool       nudge();

      void              cancel_requests();

    private:
      mpi::communicator&    comm_;
      IncomingQueuesMap     incoming_;
      OutgoingQueuesMap     outgoing_;
      InFlightList          inflight_;
      int                   expected_;
      int                   received_;
  };

  struct Communicator::Proxy
  {
                        Proxy(int gid, IncomingQueues* incoming, OutgoingQueues* outgoing):
                          gid_(gid),
                          incoming_(incoming),
                          outgoing_(outgoing)                           {}

    int                 gid() const                                     { return gid_; }

    template<class T>
    void                enqueue(const BlockID& to, const T& x) const    { OutgoingQueues& out = *outgoing_; save(out[to], x); }

    template<class T>
    void                dequeue(int from, T& x) const                   { IncomingQueues& in  = *incoming_; load(in[from], x); }

    inline void         incoming(std::vector<int>& v) const;    // fill v with every gid from which we have a message

    private:
      int               gid_;
      IncomingQueues*   incoming_;
      OutgoingQueues*   outgoing_;
  };

  struct Communicator::InFlight
  {
    BinaryBuffer        queue;
    mpi::request        request;

    // for debug purposes:
    int from;
    int to;
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

  received_ = 0;
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
{ return Proxy(gid, &incoming(gid), &outgoing(gid)); }


void
diy::Communicator::Proxy::
incoming(std::vector<int>& v) const
{
  for (IncomingQueues::const_iterator it = incoming_->begin(); it != incoming_->end(); ++it)
    v.push_back(it->first);
}

#endif
