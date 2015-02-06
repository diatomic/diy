#ifndef DIY_MASTER_HPP
#define DIY_MASTER_HPP

#include <vector>
#include <map>
#include <list>
#include <deque>

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
      typedef           std::list<Collective>               CollectivesList;
      typedef           std::map<int, CollectivesList>      CollectivesMap;     // gid          -> [collectives]


      // TODO: these types will have to be adjusted to support multi-threading
      typedef           std::map<int,     BinaryBuffer>     IncomingQueues;     //  gid         -> queue
      typedef           std::map<BlockID, BinaryBuffer>     OutgoingQueues;     // (gid, proc)  -> queue
      typedef           std::map<int,     IncomingQueues>   IncomingQueuesMap;  //  gid         -> {  gid       -> queue }
      typedef           std::map<int,     OutgoingQueues>   OutgoingQueuesMap;  //  gid         -> { (gid,proc) -> queue }


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
                      // Communicator functionality
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

      inline void   unload(int i)                       { blocks_.unload(i); }
      inline void   unload(std::vector<int>& loaded)    { blocks_.unload(loaded); }
      inline void   unload_all()                        { blocks_.unload_all(); }
      inline void   load(int i)                         { blocks_.load(i); }
      inline bool   has_incoming(int i) const;

      const mpi::communicator&  communicator() const    { return comm_; }
      mpi::communicator&        communicator()          { return comm_; }

      //! return the `i`-th block, loading it if necessary
      void*         get(int i)                          { return blocks_.get(i); }

      int           gid(int i) const                    { return gids_[i]; }
      int           lid(int gid) const                  { return local(gid) ?  lids_.find(gid)->second : -1; }
      bool          local(int gid) const                { return lids_.find(gid) != lids_.end(); }

      //! exchange the queues between all the blocks (collective operation)
      inline void   exchange();

      inline
      ProxyWithLink proxy(int i) const;

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
      IncomingQueues&   incoming(int gid)               { return incoming_[gid]; }
      OutgoingQueues&   outgoing(int gid)               { return outgoing_[gid]; }
      CollectivesList&  collectives(int gid)            { return collectives_[gid]; }

      void              set_expected(int expected)      { expected_ = expected; }
      void              add_expected(int i)             { expected_ += i; }
      int               expected() const                { return expected_; }

    public:
      // Communicator functionality
      inline void       flush();            // makes sure all the serialized queues migrate to their target processors

    private:
      // Communicator functionality
      inline void       comm_exchange();    // possibly called in between block computations
      inline bool       nudge();
      inline void       process_collectives();

      void              cancel_requests();              // TODO

    private:
      std::vector<Link*>    links_;
      Collection            blocks_;
      std::vector<int>      gids_;
      std::map<int, int>    lids_;

      int                   limit_;
      int                   threads_;

    private:
      // Communicator
      mpi::communicator     comm_;
      IncomingQueuesMap     incoming_;
      OutgoingQueuesMap     outgoing_;
      InFlightList          inflight_;
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
  const IncomingQueues& incoming_queues = const_cast<Master&>(*this).incoming(gid(i));
  for (IncomingQueues::const_iterator it = incoming_queues.begin(); it != incoming_queues.end(); ++it)
  {
    if (!it->second.empty())
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
    incoming(gid(i));
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
}

void
diy::Master::
exchange()
{
  //fprintf(stdout, "Starting exchange\n");

  // make sure there is a queue for each neighbor
  for (int i = 0; i < size(); ++i)
  {
    OutgoingQueues& outgoing_queues = outgoing(gid(i));
    if (outgoing_queues.size() < link(i)->size())
      for (unsigned j = 0; j < link(i)->size(); ++j)
        outgoing_queues[link(i)->target(j)];       // touch the outgoing queue, creating it if necessary
  }

  flush();
  //fprintf(stdout, "Finished exchange\n");
}

/* Communicator */
void
diy::Master::
comm_exchange()
{
  // isend outgoing queues
  for (OutgoingQueuesMap::iterator it = outgoing_.begin(); it != outgoing_.end(); ++it)
  {
    int from  = it->first;
    for (OutgoingQueues::iterator cur = it->second.begin(); cur != it->second.end(); ++cur)
    {
      int to   = cur->first.gid;
      int proc = cur->first.proc;

      if (proc == comm_.rank())     // sending to ourselves: simply swap buffers
      {
          incoming_[to][from] = diy::BinaryBuffer();
          incoming_[to][from].swap(cur->second);
          incoming_[to][from].reset();      // buffer position = 0
          ++received_;
          continue;
      }

      inflight_.push_back(InFlight());
      inflight_.back().from = from;
      inflight_.back().to   = to;
      inflight_.back().message.swap(cur->second);
      diy::save(inflight_.back().message, std::make_pair(from, to));
      inflight_.back().request = comm_.isend(proc, tags::queue, inflight_.back().message.buffer);
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
diy::Master::
flush()
{
#ifdef DEBUG
  time_type start = get_time();
  unsigned wait = 1;
#endif

  comm_exchange();
  while (!inflight_.empty() || received_ < expected_)
  {
    comm_exchange();

#ifdef DEBUG
    time_type cur = get_time();
    if (cur - start > wait*1000)
    {
        std::cerr << "Waiting in flush [" << comm_.rank() << "]: "
                  << inflight_.size() << " - " << received_ << " out of " << expected_ << std::endl;
        wait *= 2;
    }
#endif
  }

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
      inflight_.erase(rm);
    }
  }
  return success;
}


#endif
