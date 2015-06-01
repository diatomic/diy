#ifndef DIY_EXAMPLES_POINT_H
#define DIY_EXAMPLES_POINT_H

#include <vector>
#include <diy/types.hpp>

template<unsigned D>
struct SimplePoint
{
    float   coords[D];

    float&  operator[](unsigned i)                          { return coords[i]; }
    float   operator[](unsigned i) const                    { return coords[i]; }
};

template<unsigned DIM>
struct PointBlock
{
  typedef         SimplePoint<DIM>                            Point;
  typedef         diy::ContinuousBounds                       Bounds;

                  PointBlock(const Bounds& bounds_):
                      bounds(bounds_)                         {}

  static void*    create()                                    { return new PointBlock; }
  static void     destroy(void* b)                            { delete static_cast<PointBlock*>(b); }
  static void     save(const void* b, diy::BinaryBuffer& bb)  { diy::save(bb, *static_cast<const PointBlock*>(b)); }
  static void     load(void* b, diy::BinaryBuffer& bb)        { diy::load(bb, *static_cast<PointBlock*>(b)); }


  void            generate_points(const Bounds& domain, size_t n)
  {
    box = domain;
    points.resize(n);
    for (size_t i = 0; i < n; ++i)
      for (unsigned j = 0; j < DIM; ++j)
        points[i][j] = domain.min[j] + float(rand() % 1024)/1024 * (domain.max[j] - domain.min[j]);
  }

  void          verify_block(const diy::Master::ProxyWithLink& cp, void*)
  {
      for (size_t i = 0; i < points.size(); ++i)
        for (unsigned j = 0; j < DIM; ++j)
          if (points[i][j] < box.min[j] || points[i][j] > box.max[j])
          {
            fprintf(stderr, "!!! Point outside the box !!!\n");
            fprintf(stderr, "    %f %f %f\n", points[i][0], points[i][1], points[i][2]);
            fprintf(stderr, "    %f %f %f - %f %f %f\n",
                            box.min[0], box.min[1], box.min[2],
                            box.max[0], box.max[1], box.max[2]);
          }
  }

  void          print_block(const diy::Master::ProxyWithLink& cp, void* verbose_)
  {
      bool     verbose = *static_cast<bool*>(verbose_);

      fprintf(stdout, "[%d] Box:    %f %f %f -- %f %f %f\n",
              cp.gid(),
              box.min[0], box.min[1], box.min[2],
              box.max[0], box.max[1], box.max[2]);
      fprintf(stdout, "[%d] Bounds: %f %f %f -- %f %f %f\n",
              cp.gid(),
              bounds.min[0], bounds.min[1], bounds.min[2],
              bounds.max[0], bounds.max[1], bounds.max[2]);

      if (verbose)
      {
        for (size_t i = 0; i < points.size(); ++i)
        {
          fprintf(stdout, "  ");
          for (unsigned j = 0; j < DIM; ++j)
            fprintf(stdout, "%f ", points[i][j]);
          fprintf(stdout, "\n");
        }
      } else
          fprintf(stdout, "[%d] Points: %d\n", cp.gid(), (int) points.size());
  }

  Bounds                bounds;
  Bounds                box;
  std::vector<Point>    points;

  private:
                  PointBlock()                                  {}
};

template<unsigned DIM>
struct AddPointBlock
{
  typedef   PointBlock<DIM>                                     Block;
  typedef   diy::ContinuousBounds                               Bounds;
  typedef   diy::RegularContinuousLink                          RCLink;

        AddPointBlock(diy::Master& master_, size_t num_points_):
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

#endif
