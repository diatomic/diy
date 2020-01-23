#include <cmath>
#include <vector>

#include <diy/master.hpp>
#include <diy/link.hpp>
#include <diy/reduce.hpp>
#include <diy/partners/all-reduce.hpp>
#include <diy/partners/swap.hpp>
#include <diy/assigner.hpp>
#include <diy/mpi/io.hpp>
#include <diy/io/bov.hpp>

#include "../opts.h"

#include "sort.h"

typedef     diy::Link                   Link;
typedef     std::vector<size_t>         Histogram;

typedef     Block<Value>                ValueBlock;

// 1D sort partners:
//   these allow for k-ary reductions (as opposed to kd-trees,
//   which are fixed at k=2)
struct SortPartners
{
  // bool = are we in an exchange (vs histogram) round
  // int  = round within that partner
  typedef       std::pair<bool, int>            RoundType;

                    SortPartners(int nblocks, int k):
                        decomposer(1, diy::interval(0,nblocks-1), nblocks),
                        histogram(decomposer, k),
                        exchange(decomposer, k, false)
  {
    for (unsigned i = 0; i < exchange.rounds(); ++i)
    {
      // fill histogram rounds
      for (unsigned j = 0; j < histogram.rounds() - i; ++j)
        rounds_.push_back(std::make_pair(false, j));

      // fill exchange round
      rounds_.push_back(std::make_pair(true, i));
    }
  }

  size_t        rounds() const                              { return rounds_.size(); }
  bool          exchange_round(int round) const             { return rounds_[round].first; }
  int           sub_round(int round) const                  { return rounds_[round].second; }

  inline bool   active(int, int, const diy::Master&) const  { return true; }

  inline void   incoming(int round, int gid, std::vector<int>& partners, const diy::Master& m) const
  {
    if (round == (int) rounds())
        exchange.incoming(sub_round(round-1) + 1, gid, partners, m);
    else if (exchange_round(round))     // round != 0
        histogram.incoming(sub_round(round-1) + 1, gid, partners, m);
    else        // histogram round
    {
        if (round > 0 && sub_round(round) == 0)
            exchange.incoming(sub_round(round - 1) + 1, gid, partners, m);
        else
            histogram.incoming(sub_round(round), gid, partners, m);
    }
  }

  inline void   outgoing(int round, int gid, std::vector<int>& partners, const diy::Master& m) const
  {
    if (exchange_round(round))
        exchange.outgoing(sub_round(round), gid, partners, m);
    else
        histogram.outgoing(sub_round(round), gid, partners, m);
  }

  diy::RegularDecomposer<diy::DiscreteBounds>   decomposer;
  diy::RegularSwapPartners                      histogram;
  diy::RegularSwapPartners                      exchange;

  std::vector<RoundType>                        rounds_;
};

// Functor that tells reduce to skip histogram rounds
struct SkipHistogram
{
        SkipHistogram(const SortPartners& partners_):
            partners(partners_)                                             {}

  bool  operator()(int round, int, const diy::Master&) const                { return round < (int) partners.rounds()  &&
                                                                                    !partners.exchange_round(round) &&
                                                                                     partners.sub_round(round) != 0; }

  const SortPartners&   partners;
};

void compute_local_histogram(void* b_, const diy::ReduceProxy& srp)
{
    ValueBlock* b = static_cast<ValueBlock*>(b_);

    if (srp.round() == 0)
    {
        b->min = srp.get<Value>();
        b->max = srp.get<Value>();
    }

    // compute and enqueue local histogram
    Histogram histogram(static_cast<size_t>(b->bins));
    float width = ((float)b->max - (float)b->min) / b->bins;
    for (size_t i = 0; i < b->values.size(); ++i)
    {
        Value x = b->values[i];
        int loc = static_cast<int>(((float)x - b->min) / width);
        if (loc >= b->bins)
            loc = b->bins - 1;
        ++(histogram[static_cast<size_t>(loc)]);
    }
    for (int i = 0; i < srp.out_link().size(); ++i)
        srp.enqueue(srp.out_link().target(i), histogram);
}

void receive_histogram(void*, const diy::ReduceProxy& srp, Histogram& histogram)
{
    // dequeue and add up the histograms
    for (int i = 0; i < srp.in_link().size(); ++i)
    {
        int nbr_gid = srp.in_link().target(i).gid;

        Histogram hist;
        srp.dequeue(nbr_gid, hist);
        if (histogram.size() < hist.size())
            histogram.resize(hist.size());
        for (size_t j = 0; j < hist.size(); ++j)
            histogram[j] += hist[j];
    }
}

void add_histogram(void* b_, const diy::ReduceProxy& srp)
{
    Histogram histogram;
    receive_histogram(b_, srp, histogram);

    for (int i = 0; i < srp.out_link().size(); ++i)
        srp.enqueue(srp.out_link().target(i), histogram);
}


void enqueue_exchange(void* b_, const diy::ReduceProxy& srp, const Histogram& histogram)
{
    ValueBlock* b = static_cast<ValueBlock*>(b_);

    int k = srp.out_link().size();

    // pick split points
    size_t total = 0;
    for (size_t i = 0; i < histogram.size(); ++i)
        total += histogram[i];

    std::vector<Value>  splits;
    splits.push_back(b->min);
    size_t cur = 0;
    float width = ((float)b->max - (float)b->min) / b->bins;
    for (size_t i = 0; i < histogram.size(); ++i)
    {
        if (cur + histogram[i] > total/k*splits.size())
            splits.push_back(b->min + width*i + width/2);   // mid-point of the bin

        cur += histogram[i];

        if ((int) splits.size() == k)
            break;
    }

    // subset and enqueue
    if (srp.out_link().size() == 0)        // final round; nothing needs to be sent
        return;

    std::vector< std::vector<Value> > out_values(srp.out_link().size());
    for (size_t i = 0; i < b->values.size(); ++i)
    {
      auto loc = std::upper_bound(splits.begin(), splits.end(), b->values[i]) - splits.begin() - 1;
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
    ValueBlock* b = static_cast<ValueBlock*>(b_);

    for (int i = 0; i < srp.in_link().size(); ++i)
    {
      int nbr_gid = srp.in_link().target(i).gid;
      if (nbr_gid == srp.gid())
          continue;

      std::vector<Value>    in_values;
      srp.dequeue(nbr_gid, in_values);
      for (size_t j = 0; j < in_values.size(); ++j)
      {
        if (in_values[j] < b->min)
            throw std::runtime_error(fmt::format("{} < min = {}", in_values[j], b->min));
        b->values.push_back(in_values[j]);
      }
    }
}

void sort_local(void* b_, const diy::ReduceProxy&)
{
    ValueBlock* b = static_cast<ValueBlock*>(b_);
    std::sort(b->values.begin(), b->values.end());
}

void sort(void* b_, const diy::ReduceProxy& srp, const SortPartners& partners)
{
    if (srp.round() == partners.rounds())
    {
        dequeue_exchange(b_, srp);
        sort_local(b_, srp);
    }
    else if (partners.exchange_round(srp.round()))
    {
        Histogram histogram;
        receive_histogram(b_, srp, histogram);
        enqueue_exchange(b_, srp, histogram);
    } else if (partners.sub_round(srp.round()) == 0)
    {
        if (srp.round() > 0)
            dequeue_exchange(b_, srp);

        compute_local_histogram(b_, srp);
    } else
        add_histogram(b_, srp);
}


int main(int argc, char* argv[])
{
  diy::mpi::environment     env(argc, argv);
  diy::mpi::communicator    world;

  using namespace opts;
  Options ops;

  int               nblocks     = world.size();
  size_t            num_values  = 100;
  int               k           = 2;
  int               hist        = 32;
  int               mem_blocks  = -1;
  int               threads     = 1;
  int               chunk_size  = 1;
  std::string       prefix      = "./DIY.XXXXXX";
  bool              print, verbose, verify, help;

  Value             min = 0,
                    max = 1 << 20;

  ops
      >> Option(     "print",   print,      "print the result")
      >> Option('v', "verbose", verbose,    "verbose output")
      >> Option(     "verify",  verify,     "verify the result")
      >> Option('h', "help",    help,       "show help")
  ;

  ops
      >> Option('n', "number",  num_values,     "number of values per block")
      >> Option('k', "k",       k,              "use k-ary swap")
      >> Option(     "hist",    hist,           "histogram multiplier")
      >> Option('b', "blocks",  nblocks,        "number of blocks")
      >> Option('t', "thread",  threads,        "number of threads")
      >> Option('m', "memory",  mem_blocks,     "number of blocks to keep in memory")
      >> Option('c', "chunk",   chunk_size,     "size of a chunk in which to read the data")
      >> Option(     "prefix",  prefix,         "prefix for external storage")
  ;

  ops
      >> Option(     "min",     min,            "range min")
      >> Option(     "max",     max,            "range max")
  ;

  if (!ops.parse(argc,argv) || help)
  {
      if (world.rank() == 0)
      {
          std::cout << "Usage: " << argv[0] << " [OPTIONS] [INPUT.bov]\n";
          std::cout << "Generates random values in range [min,max] if not INPUT.bov is given.\n";
          std::cout << ops;
      }
      return 1;
  }

  std::string infile;
  ops >> PosOption(infile);

  diy::FileStorage          storage(prefix);
  diy::Master               master(world,
                                   threads,
                                   mem_blocks,
                                   &ValueBlock::create,
                                   &ValueBlock::destroy,
                                   &storage,
                                   &ValueBlock::save,
                                   &ValueBlock::load);

  diy::ContiguousAssigner   assigner(world.size(), nblocks);
  //diy::RoundRobinAssigner   assigner(world.size(), nblocks);

  std::vector<int> gids;
  assigner.local_gids(world.rank(), gids);

  if (infile.empty())
  {
    srand(static_cast<unsigned int>(time(0)));

    for (unsigned i = 0; i < gids.size(); ++i)
    {
      int             gid = gids[i];
      ValueBlock*     b   = new ValueBlock(k*hist);
      Link*           l   = new Link;

      // this could be replaced by reading values from a file
      b->generate_values(num_values, min, max);

      master.add(gid, b, l);

      // post collectives to match the external data setting
      master.proxy(i).all_reduce(min, diy::mpi::minimum<Value>());
      master.proxy(i).all_reduce(max, diy::mpi::maximum<Value>());
    }
    std::cout << "Blocks generated" << std::endl;
  } else
  {
    if (nblocks % world.size() != 0)
    {
      if (world.rank() == 0)
          fmt::print(stderr, "Number of blocks must be divisible by the number of processes (for collective MPI-IO)\n");
      return 1;
    }

    // determine the size of the file, divide by sizeof(Value)
    diy::mpi::io::file in(world, infile, diy::mpi::io::file::rdonly);
    size_t sz = in.size() / sizeof(Value);

    // We have to work around a weirdness in MPI-IO. MPI expresses various
    // sizes as integers, which is too small to reference large data. So
    // instead we create a FileBlock of larger size and read in terms of those
    // blocks.
    if (sz % (chunk_size * nblocks) != 0)
    {
      if (world.rank() == 0)
          fmt::print(stderr, "Expected data size to align with the number of blocks and chunk size\n");
      return 1;
    }

    std::vector<int> shape;
    shape.push_back(static_cast<int>(sz / chunk_size));
    diy::io::BOV reader(in, shape);

    int block_size = static_cast<int>(sz) / nblocks;

    for (unsigned i = 0; i < gids.size(); ++i)
    {
      int             gid = gids[i];
      ValueBlock*     b   = new ValueBlock(k*hist);
      Link*           l   = new Link;

      // read values from a file
      b->values.resize(static_cast<size_t>(block_size));
      diy::DiscreteBounds block_bounds(1);
      block_bounds.min[0] =  gid      * (block_size / chunk_size);
      block_bounds.max[0] = (gid + 1) * (block_size / chunk_size) - 1;

      reader.read(block_bounds, &b->values[0], true, chunk_size);
      Value block_min = *std::min_element(b->values.begin(), b->values.end());
      Value block_max = *std::max_element(b->values.begin(), b->values.end());

      master.add(gid, b, l);

      // post collectives to match the external data setting
      master.proxy(i).all_reduce(block_min, diy::mpi::minimum<Value>());
      master.proxy(i).all_reduce(block_max, diy::mpi::maximum<Value>());
    }
    if (world.rank() == 0)
      std::cout << "Array loaded" << std::endl;
  }

  // need to determine global min/max
  master.process_collectives();

  SortPartners partners(nblocks, k);
  diy::reduce(master, assigner, partners, &sort, SkipHistogram(partners));

  if (print)
  {
    fmt::print("Printing blocks\n");
    master.foreach([verbose](ValueBlock* b, const diy::Master::ProxyWithLink& cp) { b->print_block(cp, verbose); });
  }
  if (verify)
  {
    fmt::print("Verifying blocks\n");
    master.foreach(&ValueBlock::verify_block);

    master.exchange();      // to process collectives
    typedef diy::Master::ProxyWithLink      Proxy;
    int     idx   = master.loaded_block();
    Proxy   proxy = master.proxy(idx);
    size_t  total = proxy.get<size_t>();
    std::cout << "Total values: " << total << " vs " << nblocks * num_values << std::endl;

    if (world.rank() == 0)
      std::cout << "Blocks verified" << std::endl;
  }

  std::cout << "[" << world.rank() << "] Storage count:    " << storage.count() << std::endl;
  std::cout << "[" << world.rank() << "] Storage max size: " << storage.max_size() << std::endl;
}
