#ifndef DIY_MASTER_HPP
#define DIY_MASTER_HPP

#include <vector>
#include <map>
#include <list>
#include <deque>
#include <algorithm>

#include "link.hpp"
#include "collection.hpp"

// Communicator functionality
#include "mpi.hpp"
#include "serialization.hpp"
#include "detail/collectives.hpp"
#include "time.hpp"

#include "thread.hpp"

namespace diy
{
  // Stores and manages blocks; initiates serialization and communication when necessary.
  //
  // Provides a foreach function, which is meant as the main entry point.
  //
  // Provides a conversion between global and local block ids,
  // which is hidden from blocks via a communicator proxy.
  class Master
  {
    public:
      template<class Functor, class Skip>
      struct ProcessBlock;

      struct SkipNoIncoming;
      struct NeverSkip { bool    operator()(int i, const Master& master) const   { return false; } };

      typedef Collection::Create            CreateBlock;
      typedef Collection::Destroy           DestroyBlock;
      typedef Collection::Save              SaveBlock;
      typedef Collection::Load              LoadBlock;

    public:
      // Communicator types

      struct Proxy;
      struct ProxyWithLink;

      struct InFlight
      {
        BinaryBuffer        message;
        mpi::request        request;

        // for debug purposes:
        int from, to;
      };
      struct Collective;
      struct tags       { enum { queue }; };

      typedef           std::list<InFlight>                 InFlightList;
      typedef           std::list< std::pair<int, BlockID> >    ToSendList;     // [(gid, (gid, proc)]
      typedef           std::list<Collective>               CollectivesList;
      typedef           std::map<int, CollectivesList>      CollectivesMap;     // gid          -> [collectives]


      struct QueueRecord
      {
                        QueueRecord(size_t s = 0, int e = -1): size(s), external(e)     {}
        size_t          size;
        int             external;
      };

      typedef           std::map<int,     QueueRecord>      InQueueRecords;     //  gid         -> (size, external)
      typedef           std::map<int,     BinaryBuffer>     IncomingQueues;     //  gid         -> queue
      typedef           std::map<BlockID, QueueRecord>      OutQueueRecords;    // (gid, proc)  -> (size, external)
      typedef           std::map<BlockID, BinaryBuffer>     OutgoingQueues;     // (gid, proc)  -> queue
      struct IncomingQueuesRecords
      {
        InQueueRecords  records;
        IncomingQueues  queues;
      };
      struct OutgoingQueuesRecords
      {
        OutQueueRecords  records;
        OutgoingQueues   queues;
      };
      typedef           std::map<int,     IncomingQueuesRecords>    IncomingQueuesMap;  //  gid         -> {  gid       -> queue }
      typedef           std::map<int,     OutgoingQueuesRecords>    OutgoingQueuesMap;  //  gid         -> { (gid,proc) -> queue }


    public:
      // Helper functions specify how to:
      //   * create an empty block,
      //   * destroy a block (a function that's expected to upcast and delete),
      //   * serialize a block
                    Master(mpi::communicator    comm,
                           CreateBlock          create,
                           DestroyBlock         destroy,
                           int                  limit    = -1,       // blocks to store in memory
                           int                  threads  = -1,
                           ExternalStorage*     storage  = 0,
                           SaveBlock            save     = 0,
                           LoadBlock            load     = 0):
                      comm_(comm),
                      blocks_(create, destroy, storage, save, load),
                      limit_(limit),
                      threads_(threads == -1 ? thread::hardware_concurrency() : threads),
                      storage_(storage),
                      // Communicator functionality
                      inflight_size_(0),
                      expected_(0),
                      received_(0)
                                                        {}
                    ~Master()                           { destroy_block_records(); }
      inline void   destroy_block_records();
      inline void   destroy(int i)                      { blocks_.destroy(i); }

      inline int    add(int gid, void* b, Link* l);     //!< add a block
      inline void*  release(int i);                     //!< release ownership of the block

      //!< return the `i`-th block
      inline void*  block(int i) const                  { return blocks_.find(i); }
      template<class Block>
      Block*        block(int i) const                  { return static_cast<Block*>(block(i)); }
      inline Link*  link(int i) const                   { return links_[i]; }
      inline int    loaded_block() const                { return blocks_.available(); }

      inline void   unload(int i);
      inline void   load(int i);
      void          unload(std::vector<int>& loaded)    { for(unsigned i = 0; i < loaded.size(); ++i) unload(loaded[i]); loaded.clear(); }
      void          unload_all()                        { for(unsigned i = 0; i < size(); ++i) if (block(i) != 0) unload(i); }
      inline bool   has_incoming(int i) const;

      //! return the MPI communicator
      const mpi::communicator&  communicator() const    { return comm_; }
      //! return the MPI communicator
      mpi::communicator&        communicator()          { return comm_; }

      //! return the `i`-th block, loading it if necessary
      void*         get(int i)                          { return blocks_.get(i); }
      //! return gid of the `i`-th block
      int           gid(int i) const                    { return gids_[i]; }
      //! return the local id of the local block with global id gid, or -1 if not local
      int           lid(int gid) const                  { return local(gid) ?  lids_.find(gid)->second : -1; }
      //! whether the block with global id gid is local
      bool          local(int gid) const                { return lids_.find(gid) != lids_.end(); }

      //! exchange the queues between all the blocks (collective operation)
      inline void   exchange();

      inline
      ProxyWithLink proxy(int i) const;

      //! return the number of local blocks
      unsigned      size() const                        { return blocks_.size(); }
      LoadBlock     loader() const                      { return blocks_.loader(); }
      SaveBlock     saver() const                       { return blocks_.saver(); }
      void*         create() const                      { return blocks_.create(); }

      // accessors
      int           limit() const                       { return limit_; }
      int           threads() const                     { return threads_; }

      //! call `f` with every block
      template<class Functor>
      void          foreach(const Functor& f)           { foreach(f, NeverSkip(), 0); }

      template<class Functor, class T>
      void          foreach(const Functor& f, T* aux)   { foreach(f, NeverSkip(), aux); }

      template<class Functor, class Skip>
      void          foreach(const Functor& f, const Skip& skip, void* aux = 0);

    public:
      // Communicator functionality
      IncomingQueues&   incoming(int gid)               { return incoming_[gid].queues; }
      OutgoingQueues&   outgoing(int gid)               { return outgoing_[gid].queues; }
      CollectivesList&  collectives(int gid)            { return collectives_[gid]; }

      void              set_expected(int expected)      { expected_ = expected; }
      void              add_expected(int i)             { expected_ += i; }
      int               expected() const                { return expected_; }

    public:
      // Communicator functionality
      inline void       flush();            // makes sure all the serialized queues migrate to their target processors

    private:
      // Communicator functionality
      inline void       comm_exchange(ToSendList& to_send, int out_queues_limit);     // possibly called in between block computations
      inline bool       nudge();
      inline void       process_collectives();

      void              cancel_requests();              // TODO

      // debug
      inline void       show_incoming_records() const;

    private:
      std::vector<Link*>    links_;
      Collection            blocks_;
      std::vector<int>      gids_;
      std::map<int, int>    lids_;

      int                   limit_;
      int                   threads_;
      ExternalStorage*      storage_;

    private:
      // Communicator
      mpi::communicator     comm_;
      IncomingQueuesMap     incoming_;
      OutgoingQueuesMap     outgoing_;
      InFlightList          inflight_;
      size_t                inflight_size_;
      CollectivesMap        collectives_;
      int                   expected_;
      int                   received_;
  };

  template<class Functor, class Skip>
  struct Master::ProcessBlock
  {
            ProcessBlock(const Functor&             f_,
                         const Skip&                skip_,
                         void*                      aux_,
                         Master&                    master_,
                         const std::deque<int>&     blocks_,
                         int                        local_limit_,
                         critical_resource<int>&    idx_):
                f(f_), skip(skip_), aux(aux_),
                master(master_),
                blocks(blocks_),
                local_limit(local_limit_),
                idx(idx_)
            {}

    void    process()
    {
      //fprintf(stdout, "Processing with thread: %d\n",  (int) this_thread::get_id());

      std::vector<int>      local;
      do
      {
        int cur = (*idx.access())++;

        if (cur >= blocks.size())
            return;

        int i = blocks[cur];

        if (skip(i, master))
            f(0, master.proxy(i), aux);     // 0 signals that we are skipping the block (even if it's loaded)
        else
        {
            if (master.block(i) == 0)                               // block unloaded
            {
              if (local.size() == local_limit)                    // reached the local limit
                master.unload(local);

              master.load(i);
              local.push_back(i);
            }

            f(master.block(i), master.proxy(i), aux);

            // update outgoing queue records
            OutgoingQueuesRecords& out = master.outgoing_[master.gid(i)];
            for (OutgoingQueues::iterator it = out.queues.begin(); it != out.queues.end(); ++it)
              out.records[it->first] = QueueRecord(it->second.size());
        }
      } while(true);

      // TODO: invoke opportunistic communication
      //       don't forget to adjust Master::exchange()
    }

    static void run(void* bf)                   { static_cast<ProcessBlock*>(bf)->process(); }

    const Functor&          f;
    const Skip&             skip;
    void*                   aux;
    Master&                 master;
    const std::deque<int>&  blocks;
    int                     local_limit;
    critical_resource<int>& idx;
  };

  struct Master::SkipNoIncoming
  { bool operator()(int i, const Master& master) const   { return !master.has_incoming(i); } };

  struct Master::Collective
  {
            Collective():
              cop_(0)                           {}
            Collective(detail::CollectiveOp* cop):
              cop_(cop)                         {}
            // this copy constructor is very ugly, but need it to insert Collectives into a list
            Collective(const Collective& other):
              cop_(0)                           { swap(const_cast<Collective&>(other)); }
            ~Collective()                       { delete cop_; }

    void    init()                              { cop_->init(); }
    void    swap(Collective& other)             { std::swap(cop_, other.cop_); }
    void    update(const Collective& other)     { cop_->update(*other.cop_); }
    void    global(const mpi::communicator& c)  { cop_->global(c); }
    void    copy_from(Collective& other) const  { cop_->copy_from(*other.cop_); }
    void    result_out(void* x) const           { cop_->result_out(x); }

    detail::CollectiveOp*                       cop_;

    private:
    Collective& operator=(const Collective& other);
  };
}

#include "proxy.hpp"

void
diy::Master::
destroy_block_records()
{
  for (unsigned i = 0; i < size(); ++i)
  {
    destroy(i);
    delete links_[i];
  }
}

void
diy::Master::
unload(int i)
{
  //fprintf(stdout, "Unloading block: %d\n", gid(i));

  blocks_.unload(i);

  IncomingQueuesRecords& in_qrs = incoming_[gid(i)];
  for (InQueueRecords::iterator it = in_qrs.records.begin(); it != in_qrs.records.end(); ++it)
  {
    QueueRecord& qr = it->second;
    if (qr.size > 0)
    {
        //fprintf(stderr, "Unloading queue: %d <- %d\n", gid(i), it->first);
        qr.external = storage_->put(in_qrs.queues[it->first]);
    }
  }

  OutgoingQueuesRecords& out_qrs = outgoing_[gid(i)];
  for (OutQueueRecords::iterator it = out_qrs.records.begin(); it != out_qrs.records.end(); ++it)
  {
    QueueRecord& qr = it->second;
    if (qr.size > 0)
    {
        //fprintf(stderr, "Unloading queue: %d -> %d\n", gid(i), it->first.gid);
        qr.external = storage_->put(out_qrs.queues[it->first]);
    }
  }
}

void
diy::Master::
load(int i)
{
  //fprintf(stdout, "Loading block: %d\n", gid(i));

  blocks_.load(i);

  IncomingQueuesRecords& in_qrs = incoming_[gid(i)];
  for (InQueueRecords::iterator it = in_qrs.records.begin(); it != in_qrs.records.end(); ++it)
  {
    QueueRecord& qr = it->second;
    if (qr.external != -1)
    {
        //fprintf(stderr, "Loading queue: %d <- %d\n", gid(i), it->first);
        storage_->get(qr.external, in_qrs.queues[it->first]);
        qr.external = -1;
    }
  }

  OutgoingQueuesRecords& out_qrs = outgoing_[gid(i)];
  for (OutQueueRecords::iterator it = out_qrs.records.begin(); it != out_qrs.records.end(); ++it)
  {
    QueueRecord& qr = it->second;
    if (qr.external != -1)
    {
        //fprintf(stderr, "Loading queue: %d -> %d\n", gid(i), it->first.gid);
        storage_->get(qr.external, out_qrs.queues[it->first]);
        qr.external = -1;
    }
  }
}

diy::Master::ProxyWithLink
diy::Master::
proxy(int i) const
{ return ProxyWithLink(Proxy(const_cast<Master*>(this), gid(i)), block(i), link(i)); }


int
diy::Master::
add(int gid, void* b, Link* l)
{
  if (*blocks_.in_memory().const_access() == limit_)
    unload_all();

  blocks_.add(b);
  links_.push_back(l);
  gids_.push_back(gid);

  int lid = gids_.size() - 1;
  lids_[gid] = lid;
  add_expected(l->size_unique()); // NB: at every iteration we expect a message from each unique neighbor

  return lid;
}

void*
diy::Master::
release(int i)
{
  void* b = blocks_.release(i);
  delete link(i);   links_[i] = 0;
  lids_.erase(gid(i));
  return b;
}

bool
diy::Master::
has_incoming(int i) const
{
  const IncomingQueuesRecords& in_qrs = const_cast<Master&>(*this).incoming_[gid(i)];
  for (InQueueRecords::const_iterator it = in_qrs.records.begin(); it != in_qrs.records.end(); ++it)
  {
    const QueueRecord& qr = it->second;
    if (qr.size != 0)
        return true;
  }
  return false;
}

template<class Functor, class Skip>
void
diy::Master::
foreach(const Functor& f, const Skip& skip, void* aux)
{
  // touch the outgoing and incoming queues as well as collectives to make sure they exist
  for (unsigned i = 0; i < size(); ++i)
  {
    outgoing(gid(i));
    incoming(gid(i));           // implicitly touches queue records
    collectives(gid(i));
  }


  // Order the blocks, so the loaded ones come first
  std::deque<int>   blocks;
  for (unsigned i = 0; i < size(); ++i)
    if (block(i) == 0)
        blocks.push_back(i);
    else
        blocks.push_front(i);

  // don't use more threads than we can have blocks in memory
  int num_threads;
  int blocks_per_thread;
  if (limit_ == -1)
  {
    num_threads = threads_;
    blocks_per_thread = size();
  }
  else
  {
    num_threads = std::min(threads_, limit_);
    blocks_per_thread = limit_/num_threads;
  }

  // idx is shared
  critical_resource<int> idx(0);

  // launch the threads
  typedef               ProcessBlock<Functor,Skip>                      BlockFunctor;
  typedef               std::pair<thread*, BlockFunctor*>               ThreadFunctorPair;
  typedef               std::list<ThreadFunctorPair>                    ThreadFunctorList;
  ThreadFunctorList     threads;
  for (unsigned i = 0; i < num_threads; ++i)
  {
      BlockFunctor* bf = new BlockFunctor(f, skip, aux, *this, blocks, blocks_per_thread, idx);
      threads.push_back(ThreadFunctorPair(new thread(&BlockFunctor::run, bf), bf));
  }

  // join the threads
  for(typename ThreadFunctorList::iterator it = threads.begin(); it != threads.end(); ++it)
  {
      thread*           t  = it->first;
      BlockFunctor*     bf = it->second;
      t->join();
      delete t;
      delete bf;
  }

  // clear incoming queues
  incoming_.clear();
}

void
diy::Master::
exchange()
{
  //fprintf(stdout, "Starting exchange\n");

  // make sure there is a queue for each neighbor
  for (int i = 0; i < size(); ++i)
  {
    OutgoingQueues&  outgoing_queues  = outgoing_[gid(i)].queues;
    OutQueueRecords& outgoing_records = outgoing_[gid(i)].records;
    if (outgoing_queues.size() < link(i)->size() || outgoing_records.size() < link(i)->size())
      for (unsigned j = 0; j < link(i)->size(); ++j)
      {
        outgoing_queues[link(i)->target(j)];        // touch the outgoing queue, creating it if necessary
        outgoing_records[link(i)->target(j)];       // touch the outgoing record, creating it if necessary
      }
  }

  flush();
  //fprintf(stdout, "Finished exchange\n");
}

/* Communicator */
void
diy::Master::
comm_exchange(ToSendList& to_send, int out_queues_limit)
{
  // isend outgoing queues, up to the out_queues_limit
  while(inflight_size_ < out_queues_limit && !to_send.empty())
  {
    int     from    = to_send.front().first;
    BlockID to_proc = to_send.front().second;
    int     to      = to_proc.gid;
    int     proc    = to_proc.proc;
    to_send.pop_front();

    if (proc == comm_.rank())     // sending to ourselves: simply swap buffers
    {
        //fprintf(stderr, "Moving queue in-place: %d <- %d\n", to, from);

        QueueRecord& out_qr = outgoing_[from].records[to_proc];
        QueueRecord& in_qr  = incoming_[to].records[from];
        bool out_external = out_qr.external != -1;
        bool in_external  = block(lid(to)) == 0;
        if (out_external && !in_external)
        {
          // load the queue directly into its incoming place
          if (out_qr.size == 0)
            fprintf(stderr, "Warning: unexpected external empty queue\n");
          //fprintf(stderr, "Loading outgoing directly as incoming: %d <- %d\n", to, from);
          BinaryBuffer& bb = incoming_[to].queues[from];
          storage_->get(out_qr.external, bb);
          out_qr.external = -1;
          in_qr.size = bb.size();
        } else if (out_external && in_external)
        {
          // just move the records
          //fprintf(stderr, "Moving records: %d <- %d\n", to, from);
          in_qr  = out_qr;
          out_qr = QueueRecord();
        } else if (!out_external && !in_external)
        {
          //fprintf(stderr, "Swapping in memory: %d <- %d\n", to, from);
          BinaryBuffer& bb = incoming_[to].queues[from];
          bb.swap(outgoing_[from].queues[to_proc]);
          bb.reset();
          in_qr = out_qr;
          if (bb.position != 0 || bb.size() != in_qr.size || in_qr.external != -1)
              fprintf(stderr, "Warning: inconsistency after in-memory swap: %d <- %d\n", to, from);
        } else // !out_external && in_external
        {
          //fprintf(stderr, "Unloading outgoing directly as incoming: %d <- %d\n", to, from);
          diy::BinaryBuffer& bb = outgoing_[from].queues[to_proc];
          in_qr.size = bb.size();
          if (in_qr.size > 0)
              in_qr.external = storage_->put(bb);
          else
              in_qr.external = -1;
        }

        ++received_;
        continue;
    }

    inflight_.push_back(InFlight()); ++inflight_size_;
    inflight_.back().from = from;
    inflight_.back().to   = to;
    BinaryBuffer& bb = inflight_.back().message;

    QueueRecord& qr = outgoing_[from].records[to_proc];
    if (qr.external != -1)
    {
      if (qr.size == 0)
        fprintf(stderr, "Warning: unexpected external empty queue\n");
      //fprintf(stderr, "Loading queue: %d -> %d\n", to, from);
      storage_->get(qr.external, bb, 2*sizeof(int));      // extra padding for (from,to) footer
      bb.position = bb.size();
      qr.external = -1;
    } else
      bb.swap(outgoing_[from].queues[to_proc]);

    diy::save(bb, std::make_pair(from, to));
    inflight_.back().request = comm_.isend(proc, tags::queue, bb.buffer);
  }

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

    int size     = bb.size();
    int external = -1;

    incoming_[to].queues[from] = diy::BinaryBuffer();
    if (block(lid(to)) != 0)
    {
        incoming_[to].queues[from].swap(bb);
        incoming_[to].queues[from].reset();     // buffer position = 0
    } else if (size > 0)
    {
        //fprintf(stderr, "Directly unloading queue %d <- %d\n", to, from);
        external = storage_->put(bb);           // unload directly
    }
    incoming_[to].records[from] = QueueRecord(size, external);

    ++received_;

    ostatus = comm_.iprobe(mpi::any_source, tags::queue);
  }
}

void
diy::Master::
flush()
{
#ifdef DEBUG
  time_type start = get_time();
  unsigned wait = 1;
#endif

  // make a list of outgoing queues to send (the ones in memory come first)
  ToSendList    to_send;
  for (OutgoingQueuesMap::iterator it = outgoing_.begin(); it != outgoing_.end(); ++it)
  {
    OutgoingQueuesRecords& out = it->second;
    for (OutQueueRecords::iterator cur = out.records.begin(); cur != out.records.end(); ++cur)
    {
      if (cur->second.external == -1)
        to_send.push_front(std::make_pair(it->first, cur->first));
      else
        to_send.push_back(std::make_pair(it->first, cur->first));
    }
  }
  //fprintf(stderr, "to_send.size(): %lu\n", to_send.size());

  // XXX: we probably want a cleverer limit than block limit times average number of queues per block
  // XXX: with queues we could easily maintain a specific space limit
  int out_queues_limit;
  if (limit_ == -1)
    out_queues_limit = to_send.size();
  else
    out_queues_limit = std::max((size_t) 1, to_send.size()/size()*limit_);      // average number of queues per block * in-memory block limit

  do
  {
    comm_exchange(to_send, out_queues_limit);

#ifdef DEBUG
    time_type cur = get_time();
    if (cur - start > wait*1000)
    {
        fprintf(stderr, "Waiting in flush [%d]: %lu - %d out of %d\n",
                        comm_.rank(), inflight_size_, received_, expected_);
        wait *= 2;
    }
#endif
  } while (!inflight_.empty() || received_ < expected_ || !to_send.empty());

  outgoing_.clear();

  //fprintf(stderr, "Done in flush\n");
  //show_incoming_records();

  process_collectives();
  comm_.barrier();

  received_ = 0;
}

void
diy::Master::
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
    iters[0]->init();
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
diy::Master::
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
      inflight_.erase(rm); --inflight_size_;
    }
  }
  return success;
}

void
diy::Master::
show_incoming_records() const
{
  for (IncomingQueuesMap::const_iterator it = incoming_.begin(); it != incoming_.end(); ++it)
  {
    const IncomingQueuesRecords& in_qrs = it->second;
    for (InQueueRecords::const_iterator cur = in_qrs.records.begin(); cur != in_qrs.records.end(); ++cur)
    {
      const QueueRecord& qr = cur->second;
      fprintf(stderr, "%d <- %d: (size,external) = (%lu,%d)\n",
                      it->first, cur->first,
                      qr.size,
                      qr.external);
    }
    for (IncomingQueues::const_iterator cur = in_qrs.queues.begin(); cur != in_qrs.queues.end(); ++cur)
    {
      fprintf(stderr, "%d <- %d: queue.size() = %lu\n",
                      it->first, cur->first,
                      const_cast<IncomingQueuesRecords&>(in_qrs).queues[cur->first].size());
      }
  }
}

#endif
