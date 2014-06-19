#ifndef DIY_COLLECTIVES_HPP
#define DIY_COLLECTIVES_HPP

namespace diy
{
namespace detail
{
  struct CollectiveOp
  {
    virtual void    update(const CollectiveOp& other)       =0;
    virtual void    global(const mpi::communicator& comm)   =0;
    virtual void    copy_from(const CollectiveOp& other)    =0;
    virtual void    result_out(void* dest) const            =0;
    virtual         ~CollectiveOp()                         {}
  };

  template<class T, class Op>
  struct AllReduceOp: public CollectiveOp
  {
          AllReduceOp(const T& x, Op op):
            x_(x), op_(op)          {}

    void  update(const CollectiveOp& other)         { x_ = op_(x_, static_cast<const AllReduceOp&>(other).x_); }
    void  global(const mpi::communicator& comm)     { T res; mpi::all_reduce(comm, x_, res, op_); x_ = res; }
    void  copy_from(const CollectiveOp& other)      { x_ = static_cast<const AllReduceOp&>(other).x_; }
    void  result_out(void* dest) const              { *reinterpret_cast<T*>(dest) = x_; }

    private:
      T     x_;
      Op    op_;
  };
}
}

#endif
