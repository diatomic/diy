#include <cmath>

#include <diy/master.hpp>
#include <diy/global.hpp>
#include <diy/decomposition.hpp>
#include <diy/assigner.hpp>

typedef     diy::ContinuousBounds       Bounds;
typedef     diy::RegularContinuousLink  RCLink;

static const unsigned DIM = 3;

template<unsigned D>
struct SimplePoint
{
    float   coords[D];

    float&  operator[](unsigned i)                          { return coords[i]; }
    float   operator[](unsigned i) const                    { return coords[i]; }
};

struct Block
{
  typedef         SimplePoint<DIM>                            Point;

                  Block(const Bounds& bounds_):
                      bounds(bounds_)                         {}

  static void*    create()                                    { return new Block; }
  static void     destroy(void* b)                            { delete static_cast<Block*>(b); }
  static void     save(const void* b, diy::BinaryBuffer& bb)  { diy::save(bb, *static_cast<const Block*>(b)); }
  static void     load(void* b, diy::BinaryBuffer& bb)        { diy::load(bb, *static_cast<Block*>(b)); }


  void            generate_points(const Bounds& domain, size_t n)
  {
    box = domain;
    points.resize(n);
    for (size_t i = 0; i < n; ++i)
      for (unsigned j = 0; j < DIM; ++j)
        points[i][j] = domain.min[j] + float(rand() % 100)/100 * (domain.max[j] - domain.min[j]);
  }

  Bounds                bounds;
  Bounds                box;
  std::vector<Point>    points;

  private:
                  Block()                                     {}
};

struct AddBlock
{
        AddBlock(diy::Master& master_, size_t num_points_):
            master(master_),
            num_points(num_points_)
        {}

  void  operator()(int gid, const Bounds& core, const Bounds& bounds, const Bounds& domain, const RCLink& link) const
  {
    Block*          b   = new Block(core);
    RCLink*         l   = new RCLink(link);
    diy::Master&    m   = const_cast<diy::Master&>(master);

    int             lid = m.add(gid, b, l);

    b->generate_points(domain, num_points);
  }

  diy::Master&  master;
  size_t        num_points;
};

void redistribute(void* b_, const diy::SwapReduceProxy& srp, const diy::RegularPartners& partners)
{
    Block*                      b        = static_cast<Block*>(b_);
    unsigned                    round    = srp.round();

    //fprintf(stderr, "in_link.size():  %d\n", srp.in_link().count());
    //fprintf(stderr, "out_link.size(): %d\n", srp.out_link().count());

    // step 1: dequeue and merge
    // dequeue all the incoming points and add them to this block's vector
    // could use srp.incoming() instead
    for (unsigned i = 0; i < srp.in_link().count(); ++i)
    {
      int nbr_gid = srp.in_link().target(i).gid;
      if (nbr_gid == srp.gid())
          continue;

      std::vector<Block::Point>    in_points;
      srp.dequeue(nbr_gid, in_points);
      fprintf(stderr, "[%d] Received %d points from [%d]\n", srp.gid(), (int) in_points.size(), nbr_gid);
      for (size_t j = 0; j < in_points.size(); ++j)
        b->points.push_back(in_points[j]);
    }

    // step 2: subset and enqueue
    //fprintf(stderr, "[%d] out_link().count(): %d\n", srp.gid(), srp.out_link().count());
    if (srp.out_link().count() == 0)        // final round; nothing needs to be sent
        return;

    std::vector< std::vector<Block::Point> > out_points(srp.out_link().count());
    int group_size = srp.out_link().count();
    int cur_dim    = partners.dim(round);
    for (size_t i = 0; i < b->points.size(); ++i)
    {
      int loc = floor((b->points[i][cur_dim] - b->box.min[cur_dim]) / (b->box.max[cur_dim] - b->box.min[cur_dim]) * group_size);
      out_points[loc].push_back(b->points[i]);
    }
    int pos = -1;
    for (int i = 0; i < group_size; ++i)
    {
      if (srp.out_link().target(i).gid == srp.gid())
      {
        b->points.swap(out_points[i]);
        pos = i;
      }
      else
      {
        srp.enqueue(srp.out_link().target(i), out_points[i]);
        fprintf(stderr, "[%d] Sent %d points to [%d]\n", srp.gid(), (int) out_points[i].size(), srp.out_link().target(i).gid);
      }
    }
    float new_min = b->box.min[cur_dim] + (b->box.max[cur_dim] - b->box.min[cur_dim])/group_size*pos;
    float new_max = b->box.min[cur_dim] + (b->box.max[cur_dim] - b->box.min[cur_dim])/group_size*(pos + 1);
    b->box.min[cur_dim] = new_min;
    b->box.max[cur_dim] = new_max;
}

void verify_block(void* b_, const diy::Master::ProxyWithLink& cp, void*)
{
    Block*   b     = static_cast<Block*>(b_);

    for (size_t i = 0; i < b->points.size(); ++i)
      for (unsigned j = 0; j < DIM; ++j)
        if (b->points[i][j] < b->box.min[j] || b->points[i][j] > b->box.max[j])
          fprintf(stderr, "!!! Point outside the box !!!\n");
}

void print_block(void* b_, const diy::Master::ProxyWithLink& cp, void* verbose_)
{
    Block*   b       = static_cast<Block*>(b_);
    bool     verbose = *static_cast<bool*>(verbose_);

    fprintf(stdout, "[%d] Box:    %f %f %f -- %f %f %f\n",
            cp.gid(),
            b->box.min[0], b->box.min[1], b->box.min[2],
            b->box.max[0], b->box.max[1], b->box.max[2]);
    fprintf(stdout, "[%d] Bounds: %f %f %f -- %f %f %f\n",
            cp.gid(),
            b->bounds.min[0], b->bounds.min[1], b->bounds.min[2],
            b->bounds.max[0], b->bounds.max[1], b->bounds.max[2]);

    if (verbose)
    {
      for (size_t i = 0; i < b->points.size(); ++i)
      {
        fprintf(stdout, "  ");
        for (unsigned j = 0; j < DIM; ++j)
          fprintf(stdout, "%f ", b->points[i][j]);
        fprintf(stdout, "\n");
      }
    } else
        fprintf(stdout, "[%d] Points: %d\n", cp.gid(), (int) b->points.size());
}

int main(int argc, char* argv[])
{
  // TODO: read from the command line
  int                       nblocks     = 64;
  size_t                    num_points  = 100;      // points per block
  int                       mem_blocks  = -1;

  diy::mpi::environment     env(argc, argv);

  diy::mpi::communicator    world;
  diy::FileStorage          storage("./DIY.XXXXXX");
  diy::Communicator         comm(world);
  diy::Master               master(comm,
                                   &Block::create,
                                   &Block::destroy,
                                   mem_blocks,
                                   &storage,
                                   &Block::save,
                                   &Block::load);

  int   dim = DIM;
  Bounds domain;
  domain.min[0] = domain.min[1] = domain.min[2] = 0;
  domain.max[0] = domain.max[1] = domain.max[2] = 100.;

  diy::ContiguousAssigner   assigner(world.size(), nblocks);
  //diy::RoundRobinAssigner   assigner(world.size(), nblocks);
  AddBlock  create(master, num_points);
  diy::decompose(dim, world.rank(), domain, assigner, create);

  int   k = 2;
  diy::RegularPartners  partners(dim, nblocks, k, false);
  //fprintf(stderr, "%d %d %d\n", dim, nblocks, k);
  //fprintf(stderr, "partners.rounds(): %d\n", (int) partners.rounds());
  diy::swap_reduce(master, assigner, partners, redistribute);

  bool  verbose = false;
  master.foreach(print_block, &verbose);
  master.foreach(verify_block);
}
