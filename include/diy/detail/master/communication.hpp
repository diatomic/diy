namespace diy
{
    struct Master::tags             { enum { queue, piece }; };

    struct Master::MessageInfo
    {
        int from, to;
        int round;
    };

    struct Master::InFlightSend
    {
        std::shared_ptr<MemoryBuffer> message;
        mpi::request                  request;

        MessageInfo info;           // for debug purposes
    };

    struct Master::InFlightRecv
    {
        MemoryBuffer    message;
        MessageInfo     info{ -1, -1, -1 };
    };

    struct Master::IExchangeInfo
    {
      IExchangeInfo():
          n(0)                                                  {}
      IExchangeInfo(size_t n_, mpi::communicator comm_):
          n(n_),
          comm(comm_),
          global_work_(new mpi::window<int>(comm, 1))           { global_work_->lock_all(MPI_MODE_NOCHECK); }
      ~IExchangeInfo()                                          { global_work_->unlock_all(); }

      operator bool() const                                     { return n == 0; }

      int               global_work();                          // get global work status (for debugging)
      bool              all_done();                             // get global all done status
      void              reset_work();                           // reset global work counter
      int               add_work(int work);                     // add work to global work counter
      int               inc_work()                              { return add_work(1); }   // increment global work counter
      int               dec_work()                              { return add_work(-1); }  // decremnent global work counter

      size_t                              n;
      mpi::communicator                   comm;
      std::unordered_map<int, bool>       done;                 // gid -> done
      std::unique_ptr<mpi::window<int>>   global_work_;         // global work to do
    };

    // VectorWindow is used to send and receive subsets of a contiguous array in-place
    namespace detail
    {
        template <typename T>
        struct VectorWindow
        {
            T *begin;
            size_t count;
        };
    } // namespace detail

    namespace mpi
    {
    namespace detail
    {
        template<typename T>  struct is_mpi_datatype< diy::detail::VectorWindow<T> > { typedef true_type type; };

        template <typename T>
        struct mpi_datatype< diy::detail::VectorWindow<T> >
        {
            using VecWin = diy::detail::VectorWindow<T>;
            static MPI_Datatype         datatype()                { return get_mpi_datatype<T>(); }
            static const void*          address(const VecWin& x)  { return x.begin; }
            static void*                address(VecWin& x)        { return x.begin; }
            static int                  count(const VecWin& x)    { return static_cast<int>(x.count); }
        };
    }
    } // namespace mpi::detail

} // namespace diy
