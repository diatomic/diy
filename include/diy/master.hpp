#ifndef DIY_MASTER_HPP
#define DIY_MASTER_HPP

#include <vector>
#include <map>

#include "cover.hpp"

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
                           SaveBlock        save,
                           LoadBlock        load):
                      comm_(comm),
                      create_(create),
                      destroy_(destroy),
                      save_(save),
                      load_(load)                       {}
                    ~Master()                           { destroy_blocks(); }
      inline void   destroy_blocks();

      inline void   add(int gid, void* b, Link* l);
      inline void*  release(int i);                     // release ownership of the block

      inline void*  block(int i) const;
      inline Link*  link(int i) const;

      int           gid(int i) const                    { return gids_[i]; }
      int           lid(int gid) const                  { return local(gid) ?  lids_.find(gid)->second : -1; }
      bool          local(int gid) const                { return lids_.find(gid) != lids_.end(); }

      inline
      ProxyWithLink proxy(int i) const;

      unsigned      size() const                        { return blocks_.size(); }

      // f will be called with
      template<class Functor>
      void          foreach(const Functor& f);

    private:
      Communicator&                                     comm_;
      std::vector<BlockRecord*>                         blocks_;
      std::vector<int>                                  gids_;
      std::map<int, int>                                lids_;

      CreateBlock                                       create_;
      DestroyBlock                                      destroy_;
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
                          Link*                         link):
              Communicator::Proxy(proxy),
              link_(link)                                           {}

      Link* link() const                                            { return link_; }

    private:
      Link*     link_;
  };
}

void
diy::Master::
destroy_blocks()
{
  for (unsigned i = 0; i < size(); ++i)
    if (blocks_[i])
    {
      destroy_(block(i));
      delete blocks_[i];
    }
}

void*
diy::Master::
block(int i) const
{ return blocks_[i]->block; }

diy::Link*
diy::Master::
link(int i) const
{ return blocks_[i]->link; }


diy::Master::ProxyWithLink
diy::Master::
proxy(int i) const
{ return ProxyWithLink(comm_.proxy(gid(i)),  link(i)); }


void
diy::Master::
add(int gid, void* b, Link* l)
{
  blocks_.push_back(new BlockRecord(b,l));
  gids_.push_back(gid);
  lids_[gid] = gids_.size() - 1;
  comm_.add_expected(l->count()); // NB: at every iteration we expect a message from each neighbor
}

void*
diy::Master::
release(int i)
{
  void* b = block(i);
  blocks_[i] = 0;
  lids_.erase(gid(i));
  return b;
}

template<class Functor>
void
diy::Master::
foreach(const Functor& f)
{
  for (unsigned i = 0; i < size(); ++i)
  {
    f(block(i), proxy(i));
    // TODO: invoke opportunistic communication
  }
}

#endif
