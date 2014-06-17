#ifndef DIY_ASSIGNER_HPP
#define DIY_ASSIGNER_HPP

namespace diy
{
  // Derived types should define
  //   int rank(int gid) const
  // that converts a global block id to a rank that it's assigned to.
  class Assigner
  {
    public:
                    Assigner(int size, int nblocks):
                      size_(size), nblocks_(nblocks)    {}

      int           size() const                        { return size_; }
      int           nblocks() const                     { return nblocks_; }
      virtual int   rank(int gid) const     =0;

    private:
      int           size_;      // total number of ranks
      int           nblocks_;   // total number of blocks
  };

  class ContiguousAssigner: public Assigner
  {
    public:
            ContiguousAssigner(int size, int nblocks):
              Assigner(size, nblocks)           {}

      using Assigner::size;
      using Assigner::nblocks;

      int   rank(int gid) const                 { return gid / (nblocks() / size()); }
  };

  class RoundRobinAssigner: public Assigner
  {
    public:
            RoundRobinAssigner(int size, int nblocks):
              Assigner(size, nblocks)           {}

      using Assigner::size;
      using Assigner::nblocks;

      int   rank(int gid) const                 { return gid % size(); }
  };
}

#endif
