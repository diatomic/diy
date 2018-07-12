#ifndef DIY_MASTER_HPP
#define DIY_MASTER_HPP

#include <vector>
#include <map>
#include <list>
#include <deque>
#include <algorithm>
#include <functional>
#include <numeric>

#include "link.hpp"
#include "collection.hpp"

// Communicator functionality
#include "mpi.hpp"
#include "serialization.hpp"
#include "time.hpp"

#include "thread.hpp"

#include "detail/block_traits.hpp"

#include "log.hpp"
#include "stats.hpp"

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
      struct ProcessBlock;

      // Commands; forward declarations, defined in detail/master/commands.hpp
      struct BaseCommand;

      template<class Block>
      struct Command;

      using Commands = std::vector<BaseCommand*>;

      // Skip
      using Skip = std::function<bool(int, const Master&)>;

      struct SkipNoIncoming;
      struct NeverSkip { bool    operator()(int, const Master&) const { return false; } };

      // Collection
      typedef Collection::Create            CreateBlock;
      typedef Collection::Destroy           DestroyBlock;
      typedef Collection::Save              SaveBlock;
      typedef Collection::Load              LoadBlock;

    public:
      // Communicator types, defined in proxy.hpp
      struct Proxy;
      struct ProxyWithLink;
      struct IProxyWithLink;

      // foreach callback
      template<class Block>
      using Callback = std::function<void(Block*, const ProxyWithLink&)>;

      // iexchange callback
      template<class Block>
      using ICallback = std::function<bool(Block*, const IProxyWithLink&)>;

      struct QueuePolicy
      {
        virtual bool    unload_incoming(const Master& master, int from, int to, size_t size) const  =0;
        virtual bool    unload_outgoing(const Master& master, int from, size_t size) const          =0;
        virtual         ~QueuePolicy() {}
      };

      //! Move queues out of core if their size exceeds a parameter given in the constructor
      struct QueueSizePolicy: public QueuePolicy
      {
                QueueSizePolicy(size_t sz): size(sz)          {}
        bool    unload_incoming(const Master&, int, int, size_t sz) const         { return sz > size; }
        bool    unload_outgoing(const Master& master, int from, size_t sz) const  { return sz > size*master.outgoing_count(from); }

        size_t  size;
      };

      // forward declarations, defined in detail/master/communication.hpp
      struct MessageInfo;
      struct InFlightSend;
      struct InFlightRecv;
      struct tags;

      struct IExchangeInfo;

      // forward declarations, defined in detail/master/collectives.hpp
      struct Collective;

      typedef           std::list<InFlightSend>             InFlightSendsList;
      typedef           std::map<int, InFlightRecv>         InFlightRecvsMap;
      typedef           std::list<int>                      ToSendList;         // [gid]
      typedef           std::list<Collective>               CollectivesList;
      typedef           std::map<int, CollectivesList>      CollectivesMap;     // gid          -> [collectives]


      struct QueueRecord
      {
                        QueueRecord(size_t s = 0, int e = -1): size(s), external(e)     {}
        size_t          size;
        int             external;
      };

      typedef           std::map<int,     QueueRecord>      InQueueRecords;     //  gid         -> (size, external)
      typedef           std::map<int,     MemoryBuffer>     IncomingQueues;     //  gid         -> queue
      typedef           std::map<BlockID, MemoryBuffer>     OutgoingQueues;     // (gid, proc)  -> queue
      typedef           std::map<BlockID, QueueRecord>      OutQueueRecords;    // (gid, proc)  -> (size, external)
      struct IncomingQueuesRecords
      {
        InQueueRecords  records;
        IncomingQueues  queues;
      };
      struct OutgoingQueuesRecord
      {
                        OutgoingQueuesRecord(int e = -1): external(e)       {}
        int             external;
        OutQueueRecords external_local;
        OutgoingQueues  queues;
      };
      typedef           std::map<int,     IncomingQueuesRecords>    IncomingQueuesMap;  //  gid         -> {  gid       -> queue }
      typedef           std::map<int,     OutgoingQueuesRecord>     OutgoingQueuesMap;  //  gid         -> { (gid,proc) -> queue }

      struct IncomingRound
      {
        IncomingQueuesMap map;
        int received{0};
      };
      typedef std::map<int, IncomingRound> IncomingRoundMap;


    public:
     /**
      * \ingroup Initialization
      * \brief The main DIY object
      *
      * Helper functions specify how to:
           * create an empty block,
           * destroy a block (a function that's expected to upcast and delete),
           * serialize a block
      */
                    Master(mpi::communicator    comm,          //!< communicator
                           int                  threads__ = 1,  //!< number of threads DIY can use
                           int                  limit__   = -1, //!< number of blocks to store in memory
                           CreateBlock          create_   = 0,  //!< block create function; master manages creation if create != 0
                           DestroyBlock         destroy_  = 0,  //!< block destroy function; master manages destruction if destroy != 0
                           ExternalStorage*     storage   = 0,  //!< storage object (path, method, etc.) for storing temporary blocks being shuffled in/out of core
                           SaveBlock            save      = 0,  //!< block save function; master manages saving if save != 0
                           LoadBlock            load_     = 0,  //!< block load function; master manages loading if load != 0
                           QueuePolicy*         q_policy  = new QueueSizePolicy(4096)): //!< policy for managing message queues specifies maximum size of message queues to keep in memory
                      blocks_(create_, destroy_, storage, save, load_),
                      queue_policy_(q_policy),
                      limit_(limit__),
                      threads_(threads__ == -1 ? static_cast<int>(thread::hardware_concurrency()) : threads__),
                      storage_(storage),
                      // Communicator functionality
                      comm_(comm),
                      expected_(0),
                      exchange_round_(-1),
                      immediate_(true)                  {}
                    ~Master()                           { set_immediate(true); clear(); delete queue_policy_; }

      inline void   clear();
      inline void   destroy(int i)                      { if (blocks_.own()) blocks_.destroy(i); }

      inline int    add(int gid, void* b, Link* l);     //!< add a block
      inline void*  release(int i);                     //!< release ownership of the block

      //!< return the `i`-th block
      inline void*  block(int i) const                  { return blocks_.find(i); }
      template<class Block>
      Block*        block(int i) const                  { return static_cast<Block*>(block(i)); }
      //! return the `i`-th block, loading it if necessary
      void*         get(int i)                          { return blocks_.get(i); }
      template<class Block>
      Block*        get(int i)                          { return static_cast<Block*>(get(i)); }

      inline Link*  link(int i) const                   { return links_[i]; }
      inline int    loaded_block() const                { return blocks_.available(); }

      inline void   unload(int i);
      inline void   load(int i);
      void          unload(std::vector<int>& loaded)    { for(unsigned i = 0; i < loaded.size(); ++i) unload(loaded[i]); loaded.clear(); }
      void          unload_all()                        { for(unsigned i = 0; i < size(); ++i) if (block(i) != 0) unload(i); }
      inline bool   has_incoming(int i) const;

      inline void   unload_queues(int i);
      inline void   unload_incoming(int gid);
      inline void   unload_outgoing(int gid);
      inline void   load_queues(int i);
      inline void   load_incoming(int gid);
      inline void   load_outgoing(int gid);

      //! return the MPI communicator
      const mpi::communicator&  communicator() const    { return comm_; }
      //! return the MPI communicator
      mpi::communicator&        communicator()          { return comm_; }

      //! return gid of the `i`-th block
      int           gid(int i) const                    { return gids_[i]; }
      //! return the local id of the local block with global id gid, or -1 if not local
      int           lid(int gid__) const                { return local(gid__) ?  lids_.find(gid__)->second : -1; }
      //! whether the block with global id gid is local
      bool          local(int gid__) const              { return lids_.find(gid__) != lids_.end(); }

      //! exchange the queues between all the blocks (collective operation)
      inline void   exchange(bool remote = false);

      //! nonblocking exchange of the queues between all the blocks
      template<class Block>
      void          iexchange_(const ICallback<Block>&   f);

      template<class F>
      void          iexchange(const F& f)
      {
          using Block = typename detail::block_traits<F>::type;
          iexchange_<Block>(f);
      }

      inline void   process_collectives();

      inline
      ProxyWithLink proxy(int i) const;

      inline
      IProxyWithLink iproxy(int i, IExchangeInfo* iexchange) const;

      //! return the number of local blocks
      unsigned int  size() const                        { return static_cast<unsigned int>(blocks_.size()); }
      void*         create() const                      { return blocks_.create(); }

      // accessors
      int           limit() const                       { return limit_; }
      int           threads() const                     { return threads_; }
      int           in_memory() const                   { return *blocks_.in_memory().const_access(); }

      void          set_threads(int threads__)          { threads_ = threads__; }

      CreateBlock   creator() const                     { return blocks_.creator(); }
      DestroyBlock  destroyer() const                   { return blocks_.destroyer(); }
      LoadBlock     loader() const                      { return blocks_.loader(); }
      SaveBlock     saver() const                       { return blocks_.saver(); }

      //! call `f` with every block
      template<class Block>
      void          foreach_(const Callback<Block>& f, const Skip& s = NeverSkip());

      template<class F>
      void          foreach(const F& f, const Skip& s = NeverSkip())
      {
          using Block = typename detail::block_traits<F>::type;
          foreach_<Block>(f, s);
      }

      inline void   execute();

      bool          immediate() const                   { return immediate_; }
      void          set_immediate(bool i)               { if (i && !immediate_) execute(); immediate_ = i; }

    public:
      // Communicator functionality
      IncomingQueues&   incoming(int gid__)             { return incoming_[exchange_round_].map[gid__].queues; }
      OutgoingQueues&   outgoing(int gid__)             { return outgoing_[gid__].queues; }
      CollectivesList&  collectives(int gid__)          { return collectives_[gid__]; }
      size_t            incoming_count(int gid__) const
      {
        IncomingRoundMap::const_iterator round_it = incoming_.find(exchange_round_);
        if (round_it == incoming_.end())
          return 0;
        IncomingQueuesMap::const_iterator queue_it = round_it->second.map.find(gid__);
        if (queue_it == round_it->second.map.end())
          return 0;
        return queue_it->second.queues.size();
      }
      size_t            outgoing_count(int gid__) const { OutgoingQueuesMap::const_iterator it = outgoing_.find(gid__); if (it == outgoing_.end()) return 0; return it->second.queues.size(); }

      void              set_expected(int expected)      { expected_ = expected; }
      void              add_expected(int i)             { expected_ += i; }
      int               expected() const                { return expected_; }
      void              replace_link(int i, Link* link__) { expected_ -= links_[i]->size_unique(); delete links_[i]; links_[i] = link__; expected_ += links_[i]->size_unique(); }

    public:
      // Communicator functionality
      inline void       flush(bool remote = false);     // makes sure all the serialized queues migrate to their target processors

    private:
      // Communicator functionality
      inline void       comm_exchange(ToSendList& to_send, IExchangeInfo*    iexchange = 0);
      inline void       rcomm_exchange();    // possibly called in between block computations
      inline bool       nudge();
      inline void       send_outgoing_queues(ToSendList&    to_send,
                                             int            out_queues_limit,
                                             IncomingRound& current_incoming,
                                             bool           remote,
                                             IExchangeInfo* iexchange = 0);
      inline void       check_incoming_queues(IExchangeInfo* iexchange = 0);
      inline ToSendList prep_out();
      inline int        limit_out(const ToSendList& to_send);
      inline void       touch_queues();

      // iexchange commmunication
      inline void       icommunicate(IExchangeInfo* iexchange);     // async communication

      // debug
      inline void       show_incoming_records() const;

    private:
      std::vector<Link*>    links_;
      Collection            blocks_;
      std::vector<int>      gids_;
      std::map<int, int>    lids_;

      QueuePolicy*          queue_policy_;

      int                   limit_;
      int                   threads_;
      ExternalStorage*      storage_;

    private:
      // Communicator
      mpi::communicator     comm_;
      IncomingRoundMap      incoming_;
      OutgoingQueuesMap     outgoing_;
      InFlightSendsList     inflight_sends_;
      InFlightRecvsMap      inflight_recvs_;
      CollectivesMap        collectives_;
      int                   expected_;
      int                   exchange_round_;
      bool                  immediate_;
      Commands              commands_;

    private:
      fast_mutex            add_mutex_;

    public:
      std::shared_ptr<spd::logger>  log = get_logger();
      stats::Profiler               prof;
  };

  struct Master::SkipNoIncoming
  { bool operator()(int i, const Master& master) const   { return !master.has_incoming(i); } };
}

#include "detail/master/communication.hpp"
#include "detail/master/collectives.hpp"
#include "detail/master/commands.hpp"

#include "proxy.hpp"


// --- ProcessBlock ---
struct diy::Master::ProcessBlock
{
          ProcessBlock(Master&                    master_,
                       const std::deque<int>&     blocks__,
                       int                        local_limit_,
                       critical_resource<int>&    idx_):
              master(master_),
              blocks(blocks__),
              local_limit(local_limit_),
              idx(idx_)
          {}

  void    process()
  {
    master.log->debug("Processing with thread: {}",  this_thread::get_id());

    std::vector<int>      local;
    do
    {
      int cur = (*idx.access())++;

      if ((size_t)cur >= blocks.size())
          return;

      int i = blocks[cur];
      if (master.block(i))
      {
          if (local.size() == (size_t)local_limit)
              master.unload(local);
          local.push_back(i);
      }

      master.log->debug("Processing block: {}", master.gid(i));

      bool skip_block = true;
      for (size_t cmd = 0; cmd < master.commands_.size(); ++cmd)
      {
          if (!master.commands_[cmd]->skip(i, master))
          {
              skip_block = false;
              break;
          }
      }

      IncomingQueuesMap &current_incoming = master.incoming_[master.exchange_round_].map;
      if (skip_block)
      {
          if (master.block(i) == 0)
              master.load_queues(i);      // even though we are skipping the block, the queues might be necessary

          for (size_t cmd = 0; cmd < master.commands_.size(); ++cmd)
          {
              master.commands_[cmd]->execute(0, master.proxy(i));  // 0 signals that we are skipping the block (even if it's loaded)
              // TODO: is the following necessary, and if so, how to switch between proxy and iproxy
//               master.commands_[cmd]->execute(0, master.iproxy(i));  // 0 signals that we are skipping the block (even if it's loaded)

              // no longer need them, so get rid of them, rather than risk reloading
              current_incoming[master.gid(i)].queues.clear();
              current_incoming[master.gid(i)].records.clear();
          }

          if (master.block(i) == 0)
              master.unload_queues(i);    // even though we are skipping the block, the queues might be necessary
      }
      else
      {
          if (master.block(i) == 0)                             // block unloaded
          {
              if (local.size() == (size_t)local_limit)                    // reached the local limit
                  master.unload(local);

              master.load(i);
              local.push_back(i);
          }

          for (size_t cmd = 0; cmd < master.commands_.size(); ++cmd)
          {
              master.commands_[cmd]->execute(master.block(i), master.proxy(i));
              // TODO: is the following necessary, and if so, how to switch between proxy and iproxy
//               master.commands_[cmd]->execute(master.block(i), master.iproxy(i));

              // no longer need them, so get rid of them
              current_incoming[master.gid(i)].queues.clear();
              current_incoming[master.gid(i)].records.clear();
          }
      }
    } while(true);

    // TODO: invoke opportunistic communication
    //       don't forget to adjust Master::exchange()
  }

  static void run(void* bf)                   { static_cast<ProcessBlock*>(bf)->process(); }

  Master&                 master;
  const std::deque<int>&  blocks;
  int                     local_limit;
  critical_resource<int>& idx;
};
// --------------------

void
diy::Master::
clear()
{
  for (unsigned i = 0; i < size(); ++i)
    delete links_[i];
  blocks_.clear();
  links_.clear();
  gids_.clear();
  lids_.clear();
  expected_ = 0;
}

void
diy::Master::
unload(int i)
{
  log->debug("Unloading block: {}", gid(i));

  blocks_.unload(i);
  unload_queues(i);
}

void
diy::Master::
unload_queues(int i)
{
  unload_incoming(gid(i));
  unload_outgoing(gid(i));
}

void
diy::Master::
unload_incoming(int gid__)
{
  for (IncomingRoundMap::iterator round_itr = incoming_.begin(); round_itr != incoming_.end(); ++round_itr)
  {
    IncomingQueuesMap::iterator qmap_itr = round_itr->second.map.find(gid__);
    if (qmap_itr == round_itr->second.map.end())
    {
      continue;
    }
    IncomingQueuesRecords& in_qrs = qmap_itr->second;
    for (InQueueRecords::iterator it = in_qrs.records.begin(); it != in_qrs.records.end(); ++it)
    {
      QueueRecord& qr = it->second;
      if (queue_policy_->unload_incoming(*this, it->first, gid__, qr.size))
      {
        log->debug("Unloading queue: {} <- {}", gid__, it->first);
        qr.external = storage_->put(in_qrs.queues[it->first]);
      }
    }
  }
}

void
diy::Master::
unload_outgoing(int gid__)
{
  OutgoingQueuesRecord& out_qr = outgoing_[gid__];

  size_t out_queues_size = sizeof(size_t);   // map size
  size_t count = 0;
  for (OutgoingQueues::iterator it = out_qr.queues.begin(); it != out_qr.queues.end(); ++it)
  {
    if (it->first.proc == comm_.rank()) continue;

    out_queues_size += sizeof(BlockID);     // target
    out_queues_size += sizeof(size_t);      // buffer.position
    out_queues_size += sizeof(size_t);      // buffer.size
    out_queues_size += it->second.size();   // buffer contents
    ++count;
  }
  if (queue_policy_->unload_outgoing(*this, gid__, out_queues_size - sizeof(size_t)))
  {
      log->debug("Unloading outgoing queues: {} -> ...; size = {}\n", gid__, out_queues_size);
      MemoryBuffer  bb;     bb.reserve(out_queues_size);
      diy::save(bb, count);

      for (OutgoingQueues::iterator it = out_qr.queues.begin(); it != out_qr.queues.end();)
      {
        if (it->first.proc == comm_.rank())
        {
          // treat as incoming
          if (queue_policy_->unload_incoming(*this, gid__, it->first.gid, it->second.size()))
          {
            QueueRecord& qr = out_qr.external_local[it->first];
            qr.size = it->second.size();
            qr.external = storage_->put(it->second);

            out_qr.queues.erase(it++);
            continue;
          } // else keep in memory
        } else
        {
          diy::save(bb, it->first);
          diy::save(bb, it->second);

          out_qr.queues.erase(it++);
          continue;
        }
        ++it;
      }

      // TODO: this mechanism could be adjusted for direct saving to disk
      //       (without intermediate binary buffer serialization)
      out_qr.external = storage_->put(bb);
  }
}

void
diy::Master::
load(int i)
{
 log->debug("Loading block: {}", gid(i));

  blocks_.load(i);
  load_queues(i);
}

void
diy::Master::
load_queues(int i)
{
  load_incoming(gid(i));
  load_outgoing(gid(i));
}

void
diy::Master::
load_incoming(int gid__)
{
  IncomingQueuesRecords& in_qrs = incoming_[exchange_round_].map[gid__];
  for (InQueueRecords::iterator it = in_qrs.records.begin(); it != in_qrs.records.end(); ++it)
  {
    QueueRecord& qr = it->second;
    if (qr.external != -1)
    {
        log->debug("Loading queue: {} <- {}", gid__, it->first);
        storage_->get(qr.external, in_qrs.queues[it->first]);
        qr.external = -1;
    }
  }
}

void
diy::Master::
load_outgoing(int gid__)
{
  // TODO: we could adjust this mechanism to read directly from storage,
  //       bypassing an intermediate MemoryBuffer
  OutgoingQueuesRecord& out_qr = outgoing_[gid__];
  if (out_qr.external != -1)
  {
    MemoryBuffer bb;
    storage_->get(out_qr.external, bb);
    out_qr.external = -1;

    size_t count;
    diy::load(bb, count);
    for (size_t i = 0; i < count; ++i)
    {
      BlockID to;
      diy::load(bb, to);
      diy::load(bb, out_qr.queues[to]);
    }
  }
}

diy::Master::ProxyWithLink
diy::Master::
proxy(int i) const
{ return ProxyWithLink(Proxy(const_cast<Master*>(this), gid(i)), block(i), link(i)); }

diy::Master::IProxyWithLink
diy::Master::
iproxy(int i, IExchangeInfo* iexchange) const
{ return IProxyWithLink(Proxy(const_cast<Master*>(this), gid(i)), block(i), link(i), iexchange); }

int
diy::Master::
add(int gid__, void* b, Link* l)
{
  if (*blocks_.in_memory().const_access() == limit_)
    unload_all();

  lock_guard<fast_mutex>    lock(add_mutex_);       // allow to add blocks from multiple threads

  blocks_.add(b);
  links_.push_back(l);
  gids_.push_back(gid__);

  int lid__ = static_cast<int>(gids_.size()) - 1;
  lids_[gid__] = lid__;
  add_expected(l->size_unique()); // NB: at every iteration we expect a message from each unique neighbor

  return lid__;
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
  const IncomingQueuesRecords& in_qrs = const_cast<Master&>(*this).incoming_[exchange_round_].map[gid(i)];
  for (InQueueRecords::const_iterator it = in_qrs.records.begin(); it != in_qrs.records.end(); ++it)
  {
    const QueueRecord& qr = it->second;
    if (qr.size != 0)
        return true;
  }
  return false;
}

template<class Block>
void
diy::Master::
foreach_(const Callback<Block>& f, const Skip& skip)
{
    auto scoped = prof.scoped("foreach");
    DIY_UNUSED(scoped);

    commands_.push_back(new Command<Block>(f, skip));

    if (immediate())
        execute();
}

void
diy::Master::
execute()
{
  log->debug("Entered execute()");
  auto scoped = prof.scoped("execute");
  DIY_UNUSED(scoped);
  //show_incoming_records();

  // touch the outgoing and incoming queues as well as collectives to make sure they exist
  for (unsigned i = 0; i < size(); ++i)
  {
    outgoing(gid(i));
    incoming(gid(i));           // implicitly touches queue records
    collectives(gid(i));
  }

  if (commands_.empty())
      return;

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

  typedef                 ProcessBlock                                   BlockFunctor;
  if (num_threads > 1)
  {
    // launch the threads
    typedef               std::pair<thread*, BlockFunctor*>               ThreadFunctorPair;
    typedef               std::list<ThreadFunctorPair>                    ThreadFunctorList;
    ThreadFunctorList     threads;
    for (unsigned i = 0; i < (unsigned)num_threads; ++i)
    {
        BlockFunctor* bf = new BlockFunctor(*this, blocks, blocks_per_thread, idx);
        threads.push_back(ThreadFunctorPair(new thread(&BlockFunctor::run, bf), bf));
    }

    // join the threads
    for(ThreadFunctorList::iterator it = threads.begin(); it != threads.end(); ++it)
    {
        thread*           t  = it->first;
        BlockFunctor*     bf = it->second;
        t->join();
        delete t;
        delete bf;
    }
  } else
  {
      BlockFunctor bf(*this, blocks, blocks_per_thread, idx);
      BlockFunctor::run(&bf);
  }

  // clear incoming queues
  incoming_[exchange_round_].map.clear();

  if (limit() != -1 && in_memory() > limit())
      throw std::runtime_error(fmt::format("Fatal: {} blocks in memory, with limit {}", in_memory(), limit()));

  // clear commands
  for (size_t i = 0; i < commands_.size(); ++i)
      delete commands_[i];
  commands_.clear();
}

void
diy::Master::
exchange(bool remote)
{
  auto scoped = prof.scoped("exchange");
  DIY_UNUSED(scoped);

  execute();

  log->debug("Starting exchange");

#ifdef DIY_NO_MPI
  // remote doesn't need to do anything special if there is no mpi, but we also
  // can't just use it because of the ibarrier
  remote = false;
#endif

  // make sure there is a queue for each neighbor
  if (!remote)
      touch_queues();

  flush(remote);
  log->debug("Finished exchange");
}

void
diy::Master::
touch_queues()
{
  for (int i = 0; i < (int)size(); ++i)
  {
      OutgoingQueues&  outgoing_queues  = outgoing_[gid(i)].queues;
      OutQueueRecords& external_local   = outgoing_[gid(i)].external_local;
      if (outgoing_queues.size() < (size_t)link(i)->size())
          for (unsigned j = 0; j < (unsigned)link(i)->size(); ++j)
          {
              if (external_local.find(link(i)->target(j)) == external_local.end())
                  outgoing_queues[link(i)->target(j)];        // touch the outgoing queue, creating it if necessary
          }
  }
}

// iexchange()
// {
//     while !all_done
//         for all blocks
//             icommunicate
//             iproxywithlink
//             f
//             icommunicate()
// }

template<class Block>
void
diy::Master::
iexchange_(const ICallback<Block>& f)
{
    // TODO: when do the following two lines need to be executed (or do they)?
    // prepare for next round
//     incoming_.erase(exchange_round_);
//     ++exchange_round_;

    IExchangeInfo iexchange(size(), comm_);
    iexchange.add_work(size());                 // start with one work unit for each block
    comm_.barrier();                            // make sure that everyone's original work is accounted for

    int global_work_ = -1;
    int prev_global_work_ = -1;

    do
    {
        int nundeq = 0;
        int nunenq = 0;

        for (size_t i = 0; i < size(); i++)     // for all blocks
        {
            icommunicate(&iexchange);            // TODO: separate comm thread std::thread t(icommunicate);
            IProxyWithLink icp = iproxy(i, &iexchange);

            bool done = f(block<Block>(i), icp);

            int nundeq_after = 0;
            int nunenq_after = 0;
            for (size_t j = 0; j < icp.link()->size(); j++)
            {
                if (icp.incoming(icp.link()->target(j).gid))
                    ++nundeq_after;
                if (icp.outgoing(icp.link()->target(j)).size())
                    ++nunenq_after;
            }
            nundeq += nundeq_after;
            nunenq += nunenq_after;

            done &= (nundeq_after == 0);
            done &= (nunenq_after == 0);

            int gid = icp.gid();
            //fmt::print(stderr, "[{}] done = {}, result = {}\n", gid, iexchange.done[gid], done);
            if (iexchange.done[gid] != done)
            {
                iexchange.done[gid] = done;
                if (done)
                {
                    int work = iexchange.dec_work();
                    fmt::print(stderr, "[{}] Decrementing work when switching done after callback, for {}: work = {}\n", comm_.rank(), gid, work);
                }
                else
                {
                    int work = iexchange.inc_work();
                    fmt::print(stderr, "[{}] Incrementing work when switching done after callback, for {}: work = {}\n", comm_.rank(), gid, work);
                }
            }
        }

        global_work_ = iexchange.global_work();
        if (global_work_ != prev_global_work_)
        {
            int ndone = 0;
            for (auto& x : iexchange.done)
                if (x.second)
                    ndone++;
            fmt::print(stderr, "[{}] ndone = {} out of {}, nundeq = {}, nunenq = {} global_work = {}\n",
                               iexchange.comm.rank(), ndone, size(),
                               nundeq, nunenq, global_work_);
        }
        prev_global_work_ = global_work_;

    // end when all received messages have been dequeued, all blocks are done, and no messages are in flight
    } while (global_work_ > 0);
    fmt::print(stderr, "[{}] ==== Leaving iexchange ====\n", iexchange.comm.rank());
}

/* Communicator */
void
diy::Master::
comm_exchange(ToSendList& to_send, IExchangeInfo* iexchange)
{
    int out_queues_limit = limit_out(to_send);

    IncomingRound &current_incoming = incoming_[exchange_round_];
    send_outgoing_queues(to_send, out_queues_limit, current_incoming, false, iexchange);
    while(nudge());                   // kick requests
    check_incoming_queues(iexchange);
}

/* Remote communicator */

// pseudocode for rexchange protocol based on NBX algorithm of Hoefler et al.,
// Scalable Communication Protocols for Dynamic Sparse Data Exchange, 2010.
//
// rcomm_exchange()
// {
//      while (!done)
//          while (sends_in_flight < limit_on_queues_in_memory and there are unprocessed queues)
//              q = next outgoing queue (going over the in-memory queues first)
//              if (q not in memory)
//                  load q
//              issend(q)
//
//           test all requests
//           if (iprobe)
//               recv
//           if (barrier_active)
//               if (test barrier)
//                   done = true
//           else
//               if (all sends finished and all queues have been processed (including out-of-core queues))
//                   ibarrier
//                   barrier_active = true
// }
//
void
diy::Master::
rcomm_exchange()
{
    IncomingRound   &current_incoming   = incoming_[exchange_round_];
    bool            done                = false;
    bool            ibarr_act           = false;
    mpi::request    ibarr_req;                      // mpi request associated with ibarrier

    // make a list of outgoing queues to send (the ones in memory come first)
    ToSendList   to_send = prep_out();
    int out_queues_limit = limit_out(to_send);

    while (!done)
    {
        send_outgoing_queues(to_send, out_queues_limit, current_incoming, true, 0);

        // kick requests
        nudge();

        check_incoming_queues();
        if (ibarr_act)
        {
            if (ibarr_req.test())
                done = true;
        }
        else
        {
            if (to_send.empty() && inflight_sends_.empty())
            {
                ibarr_req = comm_.ibarrier();
                ibarr_act = true;
            }
        }
    }                                                 // while !done
}

// fill list of outgoing queues to send (the ones in memory come first)
// for iexchange
diy::Master::ToSendList
diy::Master::
prep_out()
{
    ToSendList to_send;

    for (OutgoingQueuesMap::iterator it = outgoing_.begin(); it != outgoing_.end(); ++it)
    {
        OutgoingQueuesRecord& out = it->second;
        if (out.external == -1)
            to_send.push_front(it->first);
        else
            to_send.push_back(it->first);
    }
    log->debug("to_send.size(): {}", to_send.size());

    return to_send;
}

// compute maximum number of queues to keep in memory
// first version just average number of queues per block * num_blocks in memory
// for iexchange
int
diy::Master::
limit_out(const ToSendList& to_send)
{
    // XXX: we probably want a cleverer limit than
    // block limit times average number of queues per block
    // XXX: with queues we could easily maintain a specific space limit
    int out_queues_limit;
    if (limit_ == -1 || size() == 0)
        return to_send.size();
    else
        // average number of queues per block * in-memory block limit
        return std::max((size_t) 1, to_send.size() / size() * limit_);
}

// iexchange communicator
void
diy::Master::
icommunicate(IExchangeInfo* iexchange)
{
    log->debug("Entering icommunicate()");

    // lock out other threads
    // TODO: not threaded yet
    // if (!CAS(comm_flag, 0, 1))
    //     return;

    // debug
//     log->info("out_queues_limit: {}", out_queues_limit);

    // exchange
    ToSendList to_send = prep_out();
    comm_exchange(to_send, iexchange);

    // cleanup

    // NB: not doing outgoing_.clear() as in Master::flush() so that outgoing queues remain in place
    // TODO: consider having a flush function for a final cleanup if the user plans to move to
    // another part of the DIY program

    log->debug("Exiting icommunicate()");
}

void
diy::Master::
send_outgoing_queues(
        ToSendList&     to_send,
        int             out_queues_limit,
        IncomingRound&  current_incoming,
        bool            remote,                     // TODO: are remote and iexchange mutually exclusive? If so, use single enum?
        IExchangeInfo*  iexchange)
{
    static const size_t MAX_MPI_MESSAGE_COUNT = INT_MAX;

    while (inflight_sends_.size() < (size_t)out_queues_limit && !to_send.empty())
    {
        int from = to_send.front();

        // deal with external_local queues
        for (OutQueueRecords::iterator it = outgoing_[from].external_local.begin(); it != outgoing_[from].external_local.end(); ++it)
        {
            int to = it->first.gid;

            log->debug("Processing local queue: {} <- {} of size {}", to, from, it->second.size);

            QueueRecord& in_qr  = current_incoming.map[to].records[from];
            bool in_external  = block(lid(to)) == 0;

            if (in_external)
                in_qr = it->second;
            else
            {
                // load the queue
                in_qr.size     = it->second.size;
                in_qr.external = -1;

                MemoryBuffer bb;
                storage_->get(it->second.external, bb);

                current_incoming.map[to].queues[from].swap(bb);
            }
            ++current_incoming.received;
        }
        outgoing_[from].external_local.clear();

        if (outgoing_[from].external != -1)
            load_outgoing(from);
        to_send.pop_front();

        OutgoingQueues& outgoing = outgoing_[from].queues;
        for (OutgoingQueues::iterator it = outgoing.begin(); it != outgoing.end(); ++it)
        {
            BlockID to_proc = it->first;
            int     to      = to_proc.gid;
            int     proc    = to_proc.proc;

            // skip empty queues
            if (iexchange && !outgoing_[from].queues[to_proc].size())
            {
                log->debug("Skipping empty queue: {} <- {}", to, from);
                continue;
            }

            log->debug("Processing queue:      {} <- {} of size {}", to, from, outgoing_[from].queues[to_proc].size());

            // There may be local outgoing queues that remained in memory

            // sending to same rank: simply swap buffers
            if (proc == comm_.rank())
            {
                log->debug("Moving queue in-place: {} <- {}", to, from);

                QueueRecord& in_qr  = current_incoming.map[to].records[from];
                bool in_external  = block(lid(to)) == 0;
                if (in_external)
                {
                    log->debug("Unloading outgoing directly as incoming: {} <- {}", to, from);
                    MemoryBuffer& bb = it->second;
                    in_qr.size = bb.size();
                    if (queue_policy_->unload_incoming(*this, from, to, in_qr.size))
                        in_qr.external = storage_->put(bb);
                    else
                    {
                        MemoryBuffer& in_bb = current_incoming.map[to].queues[from];
                        if (!iexchange)
                        {
                            in_bb.swap(bb);
                            in_bb.reset();
                        }
                        else
                        {
                            in_bb.append_binary(&bb.buffer[0], bb.size());
                            bb.clear();
                        }
                        in_qr.external = -1;
                    }
                } else        // !in_external
                {
                    log->debug("Swapping in memory:    {} <- {}", to, from);
                    MemoryBuffer& bb = current_incoming.map[to].queues[from];
                    if (!iexchange)
                    {
                        bb.swap(it->second);
                        bb.reset();
                    }
                    else
                    {
                        bb.append_binary(&it->second.buffer[0], it->second.size());
                        it->second.clear();
                    }
                    in_qr.size = bb.size();
                    in_qr.external = -1;
                }

                ++current_incoming.received;
                continue;
            }                           // sending to same rank

            // sending to a different rank
            std::shared_ptr<MemoryBuffer> buffer = std::make_shared<MemoryBuffer>();
            buffer->swap(it->second);

            MessageInfo info{from, to, exchange_round_};
            // size fits in one message
            if (buffer->size() <= (MAX_MPI_MESSAGE_COUNT - sizeof(info)))
            {
                diy::save(*buffer, info);

                inflight_sends_.emplace_back();
                inflight_sends_.back().info = info;
                if (remote || iexchange)
                {
                    if (iexchange)
                    {
                        int work = iexchange->inc_work();
                        fmt::print(stderr, "[{}] Incrementing work when sending queue: work = {}\n", comm_.rank(), work);
                    }
                    inflight_sends_.back().request = comm_.issend(proc, tags::queue, buffer->buffer);
                }
                else
                    inflight_sends_.back().request = comm_.isend(proc, tags::queue, buffer->buffer);
                inflight_sends_.back().message = buffer;
            }
            // large message gets broken into chunks
            else
            {
                int npieces = static_cast<int>((buffer->size() + MAX_MPI_MESSAGE_COUNT - 1)/MAX_MPI_MESSAGE_COUNT);

                // first send the head
                std::shared_ptr<MemoryBuffer> hb = std::make_shared<MemoryBuffer>();
                diy::save(*hb, buffer->size());
                diy::save(*hb, info);

                inflight_sends_.emplace_back();
                inflight_sends_.back().info = info;
                if (remote || iexchange)
                {
                    // add one unit of work for the entire large message (upon sending the head, not the individual pieces below)
                    if (iexchange)
                    {
                        int work = iexchange->inc_work();
                        fmt::print(stderr, "[{}] Incrementing work when sending the first piece: work = {}\n", comm_.rank(), work);
                    }
                    inflight_sends_.back().request = comm_.issend(proc, tags::piece, hb->buffer);
                }
                else
                    inflight_sends_.back().request = comm_.isend(proc, tags::piece, hb->buffer);
                inflight_sends_.back().message = hb;

                // send the message pieces
                size_t msg_buff_idx = 0;
                for (int i = 0; i < npieces; ++i, msg_buff_idx += MAX_MPI_MESSAGE_COUNT)
                {
                    int tag = (i == (npieces - 1)) ? tags::queue : tags::piece;

                    detail::VectorWindow<char> window;
                    window.begin = &buffer->buffer[msg_buff_idx];
                    window.count = std::min(MAX_MPI_MESSAGE_COUNT, buffer->size() - msg_buff_idx);

                    inflight_sends_.emplace_back();
                    inflight_sends_.back().info = info;
                    if (remote || iexchange)
                        inflight_sends_.back().request = comm_.issend(proc, tag, window);
                    else
                        inflight_sends_.back().request = comm_.isend(proc, tag, window);
                    inflight_sends_.back().message = buffer;
                }
            }                                   // large message broken into pieces
        }                                       // for (OutgoingQueues::iterator it ...
    }                                           // while (inflight_sends_.size() ...
}

void
diy::Master::
check_incoming_queues(IExchangeInfo* iexchange)
{
    mpi::optional<mpi::status> ostatus = comm_.iprobe(mpi::any_source, mpi::any_tag);
    while (ostatus)
    {
        InFlightRecv& ir = inflight_recvs_[ostatus->source()];
        ir.recv(comm_, *ostatus);     // possibly partial recv, in case of a multi-piece message

        if (ir.done)                 // all pieces assembled
        {
            assert(ir.info.round >= exchange_round_);
            IncomingRound* in = &incoming_[ir.info.round];

            bool unload = ((ir.info.round == exchange_round_) ? (block(lid(ir.info.to)) == 0) : (limit_ != -1))
                          && queue_policy_->unload_incoming(*this, ir.info.from, ir.info.to, ir.message.size());

            ir.place(in, unload, storage_, iexchange);

            ir = InFlightRecv();    // reset
        }

        ostatus = comm_.iprobe(mpi::any_source, mpi::any_tag);
    }
}

void
diy::Master::
flush(bool remote)
{
#ifdef DEBUG
  time_type start = get_time();
  unsigned wait = 1;
#endif

  // prepare for next round
  incoming_.erase(exchange_round_);
  ++exchange_round_;

  if (remote)
      rcomm_exchange();
  else
  {
      ToSendList to_send = prep_out();
      do
      {
          comm_exchange(to_send);

#ifdef DEBUG
          time_type cur = get_time();
          if (cur - start > wait*1000)
          {
              log->warn("Waiting in flush [{}]: {} - {} out of {}",
                      comm_.rank(), inflight_sends_.size(), incoming_[exchange_round_].received, expected_);
              wait *= 2;
          }
#endif
      } while (!inflight_sends_.empty() || incoming_[exchange_round_].received < expected_ || !to_send.empty());
  }

  outgoing_.clear();

  log->debug("Done in flush");
  //show_incoming_records();

  process_collectives();
}

void
diy::Master::
process_collectives()
{
  auto scoped = prof.scoped("collectives");
  DIY_UNUSED(scoped);

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
  for (InFlightSendsList::iterator it = inflight_sends_.begin(); it != inflight_sends_.end();)
  {
    mpi::optional<mpi::status> ostatus = it->request.test();
    if (ostatus)
    {
      success = true;
      it = inflight_sends_.erase(it);
    }
    else
    {
      ++it;
    }
  }
  return success;
}

void
diy::Master::
show_incoming_records() const
{
  for (IncomingRoundMap::const_iterator rounds_itr = incoming_.begin(); rounds_itr != incoming_.end(); ++rounds_itr)
  {
    for (IncomingQueuesMap::const_iterator it = rounds_itr->second.map.begin(); it != rounds_itr->second.map.end(); ++it)
    {
      const IncomingQueuesRecords& in_qrs = it->second;
      for (InQueueRecords::const_iterator cur = in_qrs.records.begin(); cur != in_qrs.records.end(); ++cur)
      {
        const QueueRecord& qr = cur->second;
        log->info("round: {}, {} <- {}: (size,external) = ({},{})",
                  rounds_itr->first,
                  it->first, cur->first,
                  qr.size,
                  qr.external);
      }
      for (IncomingQueues::const_iterator cur = in_qrs.queues.begin(); cur != in_qrs.queues.end(); ++cur)
      {
        log->info("round: {}, {} <- {}: queue.size() = {}",
                  rounds_itr->first,
                  it->first, cur->first,
                  const_cast<IncomingQueuesRecords&>(in_qrs).queues[cur->first].size());
      }
    }
  }
}

// return global work status (for debugging)
int
diy::Master::IExchangeInfo::
global_work()
{
    int global_work;
    global_work_->fetch(global_work, 0, 0);
    global_work_->flush_local(0);
    return global_work;
}

// get global all done status
bool
diy::Master::IExchangeInfo::
all_done()
{
    return global_work() == 0;
}

// reset global work counter
void
diy::Master::IExchangeInfo::
reset_work()
{
    int val = 0;
    global_work_->replace(val, 0, 0);
    global_work_->flush(0);
}

// add arbitrary units of work to global work counter
int
diy::Master::IExchangeInfo::
add_work(int work)
{
    //fmt::print(stderr, "[{}] Adding {} work\n", comm.rank(), work);
    int global_work;                                               // unused
    global_work_->fetch_and_op(&work, &global_work, 0, 0, MPI_SUM);
    global_work_->flush(0);
    if (global_work + work < 0)
        throw std::runtime_error(fmt::format("error: attempting to subtract {} units of work when global_work prior to subtraction = {}", work, global_work));
    return global_work + work;
}

#endif
