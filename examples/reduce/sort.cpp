#include <cmath>
#include <vector>

#include <diy/master.hpp>
#include <diy/link.hpp>
#include <diy/reduce.hpp>
#include <diy/partners/all-reduce.hpp>
#include <diy/partners/swap.hpp>
#include <diy/assigner.hpp>

#include "../opts.h"

//typedef     int                         Value;
typedef     float                       Value;
typedef     diy::Link                   Link;


template<class T>
T random(T min, T max);

template<>
int random(int min, int max)            { return min + rand() % (max - min); }

template<>
float random(float min, float max)      { return min + float(rand() % 1024) / 1024 * (max - min); }

template<class T>
struct Block
{
                  Block(T min_, T max_, int bins_):
                      min(min_), max(max_), bins(bins_)         {}

  static void*    create()                                      { return new Block; }
  static void     destroy(void* b)                              { delete static_cast<Block*>(b); }
  static void     save(const void* b, diy::BinaryBuffer& bb)    { diy::save(bb, *static_cast<const Block*>(b)); }
  static void     load(void* b, diy::BinaryBuffer& bb)          { diy::load(bb, *static_cast<Block*>(b)); }


  void            generate_values(size_t n)
  {
    values.resize(n);
    for (size_t i = 0; i < n; ++i)
      values[i] = random<Value>(min, max);
  }

  T                     min, max;
  std::vector<T>        values;

  int                   bins;
  std::vector<size_t>   histogram;

  private:
                  Block()                                     {}
};

// 1D sort partners:
//   these allow for k-ary reductions (as opposed to kd-trees,
//   which are fixed at k=2)
struct SortPartners
{
  // bool = are we in a swap (vs histogram) round
  // int  = round within that partner
  typedef       std::pair<bool, int>            RoundType;

                    SortPartners(int nblocks, int k):
                        histogram(1, nblocks, k),
                        swap(1, nblocks, k, false)
  {
    for (unsigned i = 0; i < swap.rounds(); ++i)
    {
      // fill histogram rounds
      for (unsigned j = 0; j < histogram.rounds(); ++j)
      {
        rounds_.push_back(std::make_pair(false, j));
        if (j == histogram.rounds() / 2 - 1 - i)
            j += 2*i;
      }

      // fill swap round
      rounds_.push_back(std::make_pair(true, i));
    }
  }

  size_t        rounds() const                              { return rounds_.size(); }
  bool          swap_round(int round) const                 { return rounds_[round].first; }
  int           sub_round(int round) const                  { return rounds_[round].second; }

  inline bool   active(int round, int gid) const
  {
    if (round == rounds())
        return true;
    else if (swap_round(round))
        return swap.active(sub_round(round), gid);
    else
        return histogram.active(sub_round(round), gid);
  }

  inline void   incoming(int round, int gid, std::vector<int>& partners) const
  {
    if (round == rounds())
        swap.incoming(sub_round(round-1) + 1, gid, partners);
    else if (swap_round(round))
        histogram.incoming(histogram.rounds(), gid, partners);
    else
    {
        if (round > 0 && sub_round(round) == 0)
            swap.incoming(sub_round(round - 1) + 1, gid, partners);
        else if (round > 0 && sub_round(round - 1) != sub_round(round) - 1)        // jump through the histogram rounds
            histogram.incoming(sub_round(round - 1) + 1, gid, partners);
        else
            histogram.incoming(sub_round(round), gid, partners);
    }
  }

  inline void   outgoing(int round, int gid, std::vector<int>& partners) const
  {
    if (round == rounds())
        swap.outgoing(sub_round(round-1) + 1, gid, partners);
    else if (swap_round(round))
        swap.outgoing(sub_round(round), gid, partners);
    else
        histogram.outgoing(sub_round(round), gid, partners);
  }

  diy::RegularAllReducePartners     histogram;
  diy::RegularSwapPartners          swap;

  std::vector<RoundType>            rounds_;
};

void compute_local_histogram(void* b_, const diy::ReduceProxy& srp)
{
    Block<Value>* b = static_cast<Block<Value>*>(b_);

    // compute and enqueue local histogram
    b->histogram.clear();
    b->histogram.resize(b->bins);
    Value   width = (b->max - b->min)/b->bins;
    for (size_t i = 0; i < b->values.size(); ++i)
    {
        Value x = b->values[i];
        int loc = (x - b->min) / width;
        if (loc >= b->bins)
            loc = b->bins - 1;
        ++(b->histogram[loc]);
    }
    if (srp.out_link().target(0).gid != srp.gid())
        srp.enqueue(srp.out_link().target(0), b->histogram);
}

void add_histogram(void* b_, const diy::ReduceProxy& srp)
{
    Block<Value>* b = static_cast<Block<Value>*>(b_);

    // dequeue and add up the histograms
    for (unsigned i = 0; i < srp.in_link().size(); ++i)
    {
        int nbr_gid = srp.in_link().target(i).gid;
        if (nbr_gid != srp.gid())
        {
            std::vector<size_t> hist;
            srp.dequeue(nbr_gid, hist);
            for (size_t i = 0; i < hist.size(); ++i)
                b->histogram[i] += hist[i];
        }
    }
    if (srp.out_link().target(0).gid != srp.gid())
        srp.enqueue(srp.out_link().target(0), b->histogram);
}

void receive_histogram(void* b_, const diy::ReduceProxy& srp)
{
    Block<Value>* b = static_cast<Block<Value>*>(b_);

    if (srp.in_link().target(0).gid != srp.gid())
        srp.dequeue(srp.in_link().target(0).gid, b->histogram);
}

void forward_histogram(void* b_, const diy::ReduceProxy& srp)
{
    Block<Value>* b = static_cast<Block<Value>*>(b_);

    for (unsigned i = 0; i < srp.out_link().size(); ++i)
        if (srp.out_link().target(i).gid != srp.gid())
            srp.enqueue(srp.out_link().target(i), b->histogram);
}

void enqueue_exchange(void* b_, const diy::ReduceProxy& srp)
{
    Block<Value>*   b        = static_cast<Block<Value>*>(b_);

    int k = srp.out_link().size();

    // pick split points
    size_t total = 0;
    for (size_t i = 0; i < b->histogram.size(); ++i)
        total += b->histogram[i];

    std::vector<Value>  splits;
    splits.push_back(b->min);
    size_t cur = 0;
    Value width = (b->max - b->min)/b->bins;
    for (size_t i = 0; i < b->histogram.size(); ++i)
    {
        if (cur + b->histogram[i] > total/k*splits.size())
            splits.push_back(b->min + width*i + width/2);   // mid-point of the bin

        cur += b->histogram[i];

        if (splits.size() == k)
            break;
    }

    // subset and enqueue
    if (srp.out_link().size() == 0)        // final round; nothing needs to be sent
        return;

    std::vector< std::vector<Value> > out_values(srp.out_link().size());
    for (size_t i = 0; i < b->values.size(); ++i)
    {
      int loc = std::upper_bound(splits.begin(), splits.end(), b->values[i]) - splits.begin() - 1;
      out_values[loc].push_back(b->values[i]);
    }
    int pos = -1;
    for (int i = 0; i < k; ++i)
    {
      if (srp.out_link().target(i).gid == srp.gid())
      {
        b->values.swap(out_values[i]);
        pos = i;
      }
      else
        srp.enqueue(srp.out_link().target(i), out_values[i]);
    }
    splits.push_back(b->max);
    Value new_min = splits[pos];
    Value new_max = splits[pos+1];
    b->min = new_min;
    b->max = new_max;
}

void dequeue_exchange(void* b_, const diy::ReduceProxy& srp)
{
    Block<Value>*   b        = static_cast<Block<Value>*>(b_);

    for (unsigned i = 0; i < srp.in_link().size(); ++i)
    {
      int nbr_gid = srp.in_link().target(i).gid;
      if (nbr_gid == srp.gid())
          continue;

      std::vector<Value>    in_values;
      srp.dequeue(nbr_gid, in_values);
      for (size_t j = 0; j < in_values.size(); ++j)
      {
        if (in_values[j] < b->min)
        {
            std::cerr << "Warning: " << in_values[j] << " < min = " << b->min << std::endl;
            std::abort();
        }
        b->values.push_back(in_values[j]);
      }
    }
}

void sort_local(void* b_, const diy::ReduceProxy&)
{
    Block<Value>*   b        = static_cast<Block<Value>*>(b_);
    std::sort(b->values.begin(), b->values.end());
}

void sort(void* b_, const diy::ReduceProxy& srp, const SortPartners& partners)
{
    if (srp.round() == partners.rounds())
    {
        dequeue_exchange(b_, srp);
        sort_local(b_, srp);
    }
    else if (partners.swap_round(srp.round()))
    {
        receive_histogram(b_, srp);
        enqueue_exchange(b_, srp);
    } else if (partners.sub_round(srp.round()) == 0)
    {
        if (srp.round() > 0)
            dequeue_exchange(b_, srp);

        compute_local_histogram(b_, srp);
    } else if (partners.sub_round(srp.round()) < partners.histogram.rounds()/2)
        add_histogram(b_, srp);
    else
    {
        receive_histogram(b_, srp);
        forward_histogram(b_, srp);
    }
}

void print_block(void* b_, const diy::Master::ProxyWithLink& cp, void* verbose_)
{
  Block<Value>*   b         = static_cast<Block<Value>*>(b_);
  bool            verbose   = *static_cast<bool*>(verbose_);

  std::cout << cp.gid() << ": " << b->min << " - " << b->max << std::endl;

  if (verbose)
    for (size_t i = 0; i < b->values.size(); ++i)
      std::cout << "  " << b->values[i] << std::endl;
}

void verify_block(void* b_, const diy::Master::ProxyWithLink& cp, void*)
{
  Block<Value>*   b   = static_cast<Block<Value>*>(b_);

  for (size_t i = 0; i < b->values.size(); ++i)
    if (b->values[i] < b->min || b->values[i] > b->max)
      std::cout << "Warning: " << b->values[i] << " outside of [" << b->min << "," << b->max << "]" << std::endl;
}


int main(int argc, char* argv[])
{
  diy::mpi::environment     env(argc, argv);
  diy::mpi::communicator    world;

  using namespace opts;
  Options ops(argc, argv);

  int               nblocks     = world.size();
  size_t            num_values  = 100;
  int               k           = 2;
  int               hist        = 32;
  int               mem_blocks  = -1;
  int               threads     = 1;
  std::string       prefix      = "./DIY.XXXXXX";
  bool              verbose     = ops >> Present('v', "verbose", "verbose output");

  Value             min = 0,
                    max = 1 << 20;

  ops
      >> Option('n', "number",  num_values,     "number of values per block")
      >> Option('k', "k",       k,              "use k-ary swap")
      >> Option(     "hist",    hist,           "histogram multiplier")
      >> Option('b', "blocks",  nblocks,        "number of blocks")
      >> Option('t', "thread",  threads,        "number of threads")
      >> Option(     "prefix",  prefix,         "prefix for external storage")
  ;

  ops
      >> Option(     "min",     min,            "range min")
      >> Option(     "max",     max,            "range max")
  ;

  if (ops >> Present('h', "help", "show help"))
  {
      if (world.rank() == 0)
      {
          std::cout << "Usage: " << argv[0] << " [OPTIONS]\n";
          std::cout << ops;
      }
      return 1;
  }

  diy::FileStorage          storage(prefix);
  diy::Communicator         comm(world);
  diy::Master               master(comm,
                                   &Block<Value>::create,
                                   &Block<Value>::destroy,
                                   mem_blocks,
                                   threads,
                                   &storage,
                                   &Block<Value>::save,
                                   &Block<Value>::load);

  srand(time(0));

  diy::ContiguousAssigner   assigner(world.size(), nblocks);
  //diy::RoundRobinAssigner   assigner(world.size(), nblocks);

  // initially fill the blocks with random points anywhere in the domain
  std::vector<int> gids;
  assigner.local_gids(world.rank(), gids);
  for (unsigned i = 0; i < gids.size(); ++i)
  {
    int             gid = gids[i];
    Block<Value>*   b   = new Block<Value>(min, max, k*hist);
    Link*           l   = new Link;

    // this could be replaced by reading values from a file
    b->generate_values(num_values);

    master.add(gid, b, l);
  }
  std::cout << "Blocks generated" << std::endl;

  SortPartners partners(nblocks, k);
  diy::reduce(master, assigner, partners, sort);

  master.foreach(print_block, &verbose);
  master.foreach(verify_block);
  if (world.rank() == 0)
    std::cout << "Blocks verified" << std::endl;
}
