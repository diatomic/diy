#include <cmath>
#include <vector>

#include <diy/master.hpp>
#include <diy/link.hpp>
#include <diy/reduce.hpp>
#include <diy/partners/all-reduce.hpp>
#include <diy/partners/swap.hpp>
#include <diy/assigner.hpp>

#include "../opts.h"

typedef     diy::Link                   Link;
typedef     diy::ContinuousBounds       Bounds;

static const unsigned DIM = 3;

template<unsigned D>
struct SimplePoint
{
    float   coords[D];

    float&  operator[](unsigned i)                          { return coords[i]; }
    float   operator[](unsigned i) const                    { return coords[i]; }
};

float random(float min, float max)      { return min + float(rand() % 1024) / 1024 * (max - min); }

struct Block
{
  typedef         SimplePoint<DIM>                            Point;

                  Block(const Bounds& domain_, int bins_):
                      domain(domain_), bins(bins_)         {}

  static void*    create()                                      { return new Block; }
  static void     destroy(void* b)                              { delete static_cast<Block*>(b); }
  static void     save(const void* b, diy::BinaryBuffer& bb)    { diy::save(bb, *static_cast<const Block*>(b)); }
  static void     load(void* b, diy::BinaryBuffer& bb)          { diy::load(bb, *static_cast<Block*>(b)); }


  void            generate_points(size_t n)
  {
    box = domain;
    points.resize(n);
    for (size_t i = 0; i < n; ++i)
      for (unsigned j = 0; j < DIM; ++j)
        points[i][j] = random(domain.min[j], domain.max[j]);
  }

  Bounds                domain;
  Bounds                box;
  std::vector<Point>    points;

  int                   bins;
  std::vector<size_t>   histogram;

  private:
                  Block()                                  {}
};

struct KDTreePartners
{
  // bool = are we in a swap (vs histogram) round
  // int  = round within that partner
  typedef           std::pair<bool, int>                    RoundType;

                    KDTreePartners(int dim, int nblocks):
                        histogram(1, nblocks, 2),
                        swap(1, nblocks, 2, false)
  {
    for (unsigned i = 0; i < swap.rounds(); ++i)
    {
      // fill histogram rounds
      for (unsigned j = 0; j < histogram.rounds(); ++j)
      {
        rounds_.push_back(std::make_pair(false, j));
        dim_.push_back(i % dim);
        if (j == histogram.rounds() / 2 - 1 - i)
            j += 2*i;
      }

      // fill swap round
      rounds_.push_back(std::make_pair(true, i));
      dim_.push_back(i % dim);
    }
  }

  size_t        rounds() const                              { return rounds_.size(); }
  int           dim(int round) const                        { return dim_[round]; }
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
  std::vector<int>                  dim_;
};

void compute_local_histogram(void* b_, const diy::ReduceProxy& srp, int dim)
{
    Block* b = static_cast<Block*>(b_);

    // compute and enqueue local histogram
    b->histogram.clear();
    b->histogram.resize(b->bins);
    float   width = (b->box.max[dim] - b->box.min[dim])/b->bins;
    for (size_t i = 0; i < b->points.size(); ++i)
    {
        float x = b->points[i][dim];
        int loc = (x - b->box.min[dim]) / width;
        if (loc < 0)
        {
            std::cerr << loc << " " << x << " " << b->box.min[dim] << std::endl;
            std::abort();
        }
        if (loc >= b->bins)
            loc = b->bins - 1;
        ++(b->histogram[loc]);
    }
    if (srp.out_link().target(0).gid != srp.gid())
        srp.enqueue(srp.out_link().target(0), b->histogram);
}

void add_histogram(void* b_, const diy::ReduceProxy& srp)
{
    Block* b = static_cast<Block*>(b_);

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
    Block* b = static_cast<Block*>(b_);

    if (srp.in_link().target(0).gid != srp.gid())
        srp.dequeue(srp.in_link().target(0).gid, b->histogram);
}

void forward_histogram(void* b_, const diy::ReduceProxy& srp)
{
    Block* b = static_cast<Block*>(b_);

    for (unsigned i = 0; i < srp.out_link().size(); ++i)
        if (srp.out_link().target(i).gid != srp.gid())
            srp.enqueue(srp.out_link().target(i), b->histogram);
}

void enqueue_exchange(void* b_, const diy::ReduceProxy& srp, int dim)
{
    Block*   b        = static_cast<Block*>(b_);

    int k = srp.out_link().size();

    // pick split points
    size_t total = 0;
    for (size_t i = 0; i < b->histogram.size(); ++i)
        total += b->histogram[i];

    size_t cur   = 0;
    float  width = (b->box.max[dim] - b->box.min[dim])/b->bins;
    float  split;
    for (size_t i = 0; i < b->histogram.size(); ++i)
    {
        if (cur + b->histogram[i] > total/2)
        {
            split = b->box.min[dim] + width*i + width/2;   // mid-point of the bin
            break;
        }

        cur += b->histogram[i];
    }

    // subset and enqueue
    if (srp.out_link().size() == 0)        // final round; nothing needs to be sent
        return;

    std::vector< std::vector<Block::Point> > out_points(srp.out_link().size());
    for (size_t i = 0; i < b->points.size(); ++i)
    {
      float x = b->points[i][dim];
      int loc = x < split ? 0 : 1;
      out_points[loc].push_back(b->points[i]);
    }
    int pos = -1;
    for (int i = 0; i < k; ++i)
    {
      if (srp.out_link().target(i).gid == srp.gid())
      {
        b->points.swap(out_points[i]);
        pos = i;
      }
      else
        srp.enqueue(srp.out_link().target(i), out_points[i]);
    }
    if (pos == 0)
        b->box.max[dim] = split;
    else
        b->box.min[dim] = split;
}

void dequeue_exchange(void* b_, const diy::ReduceProxy& srp, int dim)
{
    Block*   b        = static_cast<Block*>(b_);

    for (unsigned i = 0; i < srp.in_link().size(); ++i)
    {
      int nbr_gid = srp.in_link().target(i).gid;
      if (nbr_gid == srp.gid())
          continue;

      std::vector<Block::Point>    in_points;
      srp.dequeue(nbr_gid, in_points);
      for (size_t j = 0; j < in_points.size(); ++j)
      {
        if (in_points[j][dim] < b->box.min[dim] || in_points[j][dim] > b->box.max[dim])
        {
            fprintf(stderr, "Warning: dequeued %f outside [%f,%f] (%d)\n",
                            in_points[j][dim], b->box.min[dim], b->box.max[dim], dim);
            std::abort();
        }
        b->points.push_back(in_points[j]);
      }
    }
}

void partition(void* b_, const diy::ReduceProxy& srp, const KDTreePartners& partners)
{
    int dim;
    if (srp.round() < partners.rounds())
        dim = partners.dim(srp.round());
    else
        dim = partners.dim(srp.round() - 1);

    if (srp.round() == partners.rounds())
        dequeue_exchange(b_, srp, dim);
    else if (partners.swap_round(srp.round()))
    {
        receive_histogram(b_, srp);
        enqueue_exchange(b_, srp, dim);
    } else if (partners.sub_round(srp.round()) == 0)
    {
        if (srp.round() > 0)
            dequeue_exchange(b_, srp, DIM - 1 - dim);

        compute_local_histogram(b_, srp, dim);
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
  Block*   b         = static_cast<Block*>(b_);
  bool            verbose   = *static_cast<bool*>(verbose_);

  fprintf(stdout, "%d: [%f,%f,%f] - [%f,%f,%f]\n",
                  cp.gid(),
                  b->box.min[0], b->box.min[1], b->box.min[2],
                  b->box.max[0], b->box.max[1], b->box.max[2]);

  if (verbose)
    for (size_t i = 0; i < b->points.size(); ++i)
      fprintf(stdout, "  %f %f %f\n", b->points[i][0], b->points[i][1], b->points[i][2]);
}

void verify_block(void* b_, const diy::Master::ProxyWithLink& cp, void*)
{
  Block*   b   = static_cast<Block*>(b_);

  for (size_t i = 0; i < b->points.size(); ++i)
    for (int j = 0; j < DIM; ++j)
      if (b->points[i][j] < b->box.min[j] || b->points[i][j] > b->box.max[j])
        fprintf(stdout, "Warning: %f outside of [%f,%f] (%d)\n", b->points[i][j], b->box.min[j], b->box.max[j], j);
}


int main(int argc, char* argv[])
{
  diy::mpi::environment     env(argc, argv);
  diy::mpi::communicator    world;

  using namespace opts;
  Options ops(argc, argv);

  int               nblocks     = world.size();
  size_t            num_points  = 100;
  int               hist        = 32;
  int               mem_blocks  = -1;
  int               threads     = 1;
  std::string       prefix      = "./DIY.XXXXXX";
  bool              verbose     = ops >> Present('v', "verbose", "verbose output");

  ops
      >> Option('n', "number",  num_points,     "number of points per block")
      >> Option(     "hist",    hist,           "histogram multiplier")
      >> Option('b', "blocks",  nblocks,        "number of blocks")
      >> Option('t', "thread",  threads,        "number of threads")
      >> Option(     "prefix",  prefix,         "prefix for external storage")
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
  diy::Master               master(world,
                                   threads,
                                   mem_blocks,
                                   &Block::create,
                                   &Block::destroy,
                                   &storage,
                                   &Block::save,
                                   &Block::load);

  //srand(time(0));

  diy::ContiguousAssigner   assigner(world.size(), nblocks);
  //diy::RoundRobinAssigner   assigner(world.size(), nblocks);

  Bounds domain;
  domain.min[0] = domain.min[1] = domain.min[2] = 0;
  domain.max[0] = domain.max[1] = domain.max[2] = 1000;

  // initially fill the blocks with random points anywhere in the domain
  std::vector<int> gids;
  assigner.local_gids(world.rank(), gids);
  for (unsigned i = 0; i < gids.size(); ++i)
  {
    int             gid = gids[i];
    Block*          b   = new Block(domain, 2*hist);
    Link*           l   = new Link;

    // this could be replaced by reading values from a file
    b->generate_points(num_points);

    master.add(gid, b, l);
  }
  std::cout << "Blocks generated" << std::endl;

  KDTreePartners partners(DIM, nblocks);
  diy::reduce(master, assigner, partners, partition);

  master.foreach(print_block, &verbose);
  master.foreach(verify_block);
  if (world.rank() == 0)
    std::cout << "Blocks verified" << std::endl;
}
