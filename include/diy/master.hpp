#ifndef DIY_MASTER_HPP
#define DIY_MASTER_HPP

#include <vector>
#include <map>

#include "communicator.hpp"
#include "link.hpp"
#include "storage.hpp"

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
      struct BlockRecord;
      struct ProxyWithLink;

      typedef       void* (*CreateBlock)();
      typedef       void  (*DestroyBlock)(void*);
      typedef       void  (*SaveBlock)(const void*, BinaryBuffer& buf);
      typedef       void  (*LoadBlock)(void*,       BinaryBuffer& buf);

    public:
      // Helper functions specify how to:
      //   * create an empty block,
      //   * destroy a block (a function that's expected to upcast and delete),
      //   * serialize a block
                    Master(Communicator&    comm,
                           CreateBlock      create,
                           DestroyBlock     destroy,
                           int              limit    = -1,       // blocks to store in memory
                           ExternalStorage* storage  = 0,
                           SaveBlock        save     = 0,
                           LoadBlock        load     = 0):
                      comm_(comm),
                      create_(create),
                      destroy_(destroy),
                      limit_(limit),
                      in_memory_(0),
                      storage_(storage),
                      save_(save),
                      load_(load)                       {}
                    ~Master()                           { destroy_block_records(); }
      inline void   destroy_block_records();
      inline void   destroy(int i);

      inline int    add(int gid, void* b, Link* l);
      inline void*  release(int i);                     // release ownership of the block

      inline void*  block(int i) const;
      template<class Block>
      Block*        block(int i) const                  { return static_cast<Block*>(block(i)); }
      inline Link*  link(int i) const;
      inline int    loaded_block() const                { int i = 0; for (; i < size(); ++i) if (block(i) != 0) break; return i; }

      inline void   unload_all();
      inline void   unload(int i);
      inline void   load(int i);
      inline bool   has_incoming(int i) const;

      // load if necessary
      void*         get(int i)                          { if (block(i) == 0) load(i); return block(i); }

      int           gid(int i) const                    { return gids_[i]; }
      int           lid(int gid) const                  { return local(gid) ?  lids_.find(gid)->second : -1; }
      bool          local(int gid) const                { return lids_.find(gid) != lids_.end(); }

      inline void   exchange();

      inline
      ProxyWithLink proxy(int i) const;

      unsigned      size() const                        { return blocks_.size(); }
      LoadBlock     loader() const                      { return load_; }
      SaveBlock     saver() const                       { return save_; }
      void*         create() const                      { return create_(); }

      // f will be called with
      template<class Functor>
      void          foreach(const Functor& f, void* aux = 0, bool load_on_incoming = false);

    protected:
      inline void*& block(int i);

    private:
      Communicator&                                     comm_;
      std::vector<BlockRecord*>                         blocks_;
      std::vector<int>                                  external_;
      std::vector<int>                                  gids_;
      std::map<int, int>                                lids_;

      CreateBlock                                       create_;
      DestroyBlock                                      destroy_;

      int                                               limit_;
      int                                               in_memory_;
      ExternalStorage*                                  storage_;
      SaveBlock                                         save_;
      LoadBlock                                         load_;
  };

  struct Master::BlockRecord
  {
            BlockRecord(void* block_, Link* link_):
              block(block_), link(link_)                    {}
            ~BlockRecord()                                  { delete link; }

    void*   block;      // the block is assumed to be properly destroyed before this object is deleted
    Link*   link;       // takes ownership of the link

    private:
        // disallow copies for now
                    BlockRecord(const BlockRecord& other)   {}
      BlockRecord&  operator=(const BlockRecord& o)         { return *this; }
  };

  struct Master::ProxyWithLink: public Communicator::Proxy
  {
            ProxyWithLink(const Communicator::Proxy&    proxy,
                          void*                         block,
                          Link*                         link):
              Communicator::Proxy(proxy),
              block_(block),
              link_(link)                                           {}

      Link*   link() const                                          { return link_; }
      void*   block() const                                         { return block_; }

    private:
      void*   block_;
      Link*   link_;
  };
}

void
diy::Master::
destroy_block_records()
{
  for (unsigned i = 0; i < size(); ++i)
  {
    destroy(i);
    delete blocks_[i];
    if (external_[i] != -1)
      storage_->destroy(i);
  }
}

void
diy::Master::
destroy(int i)
{
  if (block(i))
    destroy_(block(i));
}

void*
diy::Master::
block(int i) const
{ return blocks_[i]->block; }

void*&
diy::Master::
block(int i)
{ return blocks_[i]->block; }

diy::Link*
diy::Master::
link(int i) const
{ return blocks_[i]->link; }


diy::Master::ProxyWithLink
diy::Master::
proxy(int i) const
{ return ProxyWithLink(comm_.proxy(gid(i)), block(i), link(i)); }


int
diy::Master::
add(int gid, void* b, Link* l)
{
  if (in_memory_ == limit_)
    unload_all();

  blocks_.push_back(new BlockRecord(b,l));
  external_.push_back(-1);
  gids_.push_back(gid);
  int lid = gids_.size() - 1;
  lids_[gid] = lid;
  comm_.add_expected(l->count()); // NB: at every iteration we expect a message from each neighbor

  ++in_memory_;

  return lid;
}

void*
diy::Master::
release(int i)
{
  if (block(i) == 0)
  {
    // assert(external_[i] != -1)
    load(i);
  }

  void* b = block(i);
  block(i) = 0;
  delete link(i);   blocks_[i]->link = 0;
  lids_.erase(gid(i));
  return b;
}

void
diy::Master::
unload_all()
{
  for (unsigned i = 0; i < size(); ++i)
    if (block(i))
      unload(i);
}

void
diy::Master::
unload(int i)
{
  // TODO: could avoid the extra copy by asking storage_ for an instance derived
  //       from BinaryBuffer, which could save the data directly

  BinaryBuffer bb;
  save_(block(i), bb);
  external_[i] = storage_->put(bb);

  destroy_(block(i));
  block(i) = 0;

  --in_memory_;
}

void
diy::Master::
load(int i)
{
  BinaryBuffer bb;
  storage_->get(external_[i], bb);
  void* b = create_();
  load_(b, bb);
  block(i) = b;
  external_[i] = -1;

  ++in_memory_;
}

bool
diy::Master::
has_incoming(int i) const
{
  const Communicator::IncomingQueues& incoming = const_cast<Communicator&>(comm_).incoming(gid(i));
  for (Communicator::IncomingQueues::const_iterator it = incoming.begin(); it != incoming.end(); ++it)
  {
    if (!it->second.empty())
        return true;
  }
  return false;
}

template<class Functor>
void
diy::Master::
foreach(const Functor& f, void* aux, bool load_on_incoming)
{
  for (unsigned i = 0; i < size(); ++i)
  {
    if (block(i) == 0 && external_[i] != -1)
      if (!load_on_incoming || has_incoming(i))
      {
          if (in_memory_ == limit_)
            unload_all();
          load(i);
      }

    f(block(i), proxy(i), aux);
    // TODO: invoke opportunistic communication
    //       don't forget to adjust Master::exchange()
  }
}

void
diy::Master::
exchange()
{
  // make sure there is a queue for each neighbor
  for (int i = 0; i < size(); ++i)
  {
    Communicator::OutgoingQueues& outgoing = comm_.outgoing(gid(i));
    if (outgoing.size() < link(i)->count())
      for (unsigned j = 0; j < link(i)->count(); ++j)
        outgoing[link(i)->target(j)];       // touch the outgoing queue, creating it if necessary
  }

  comm_.flush();
}

#endif
