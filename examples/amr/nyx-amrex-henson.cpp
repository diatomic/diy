#include <iostream>
#include <stdexcept>
#include <vector>
#include <array>

#include <Nyx.H>

#include <henson/context.h>
#include <henson/data.h>

#include <diy/master.hpp>
#include <diy/assigner.hpp>
#include <diy/link.hpp>
#include <diy/resolve.hpp>
#include <diy/io/block.hpp>         // for saving blocks in DIY format

#include <diy/thirdparty/fmt/format.h>

#include "fab-block.h"
using Block = FabBlock<amrex::Real>;

diy::AMRLink::Bounds bounds(const amrex::Box& box)
{
    diy::AMRLink::Bounds bounds(3);
    for (int i = 0; i < 3; ++i)
    {
        bounds.min[i] = box.loVect()[i];
        bounds.max[i] = box.hiVect()[i];
    }
    return bounds;
}

int main()
{
    using namespace amrex;

    MPI_Comm world_; MPI_Comm_dup(henson_get_world(), &world_);
    diy::mpi::communicator world(world_, true);
    diy::Master master(world, 1, -1,
                       Block::create,
                       Block::destroy,
                       0,
                       Block::save,
                       Block::load);

    Amr* amr;
    henson_load_pointer("amr", (void**) &amr);

    int periodic = 0;
    std::array<bool, 3> is_periodic;
    henson_load_int("periodic", &periodic);
    for (int i = 0; i < 3; ++i)
        is_periodic[i] = periodic & (1 << i);

    const Box&  domain       = amr->getLevel(0).Domain();
    auto        finest_level = amr->finestLevel();

    int nblocks = 0;
    std::vector<int> gid_offsets = { 0 };
    std::vector<int> refinements = { 1 };
    for (int lev = 0; lev <= finest_level; lev++)
    {
        const MultiFab& mf = amr->getLevel(lev).get_old_data(PhiGrav_Type);
        const BoxArray& ba = mf.boxArray();
        nblocks += ba.size();
        gid_offsets.push_back(nblocks);

        const IntVect&  refinement = amr->getLevel(lev).fineRatio();
        if (refinement[0] != refinement[1] || refinement[0] != refinement[2])
            throw std::runtime_error("Unexpected uneven refinement");

        refinements.push_back(refinements.back() * refinement[0]);
    }

    for (int lev = 0; lev <= finest_level; lev++)
    {
        const MultiFab&                 mf = amr->getLevel(lev).get_old_data(PhiGrav_Type);       // TODO: might want a different way to specify the data type we want
        const BoxArray&                 ba = mf.boxArray();

        std::vector<std::pair<int,Box>> isects;
        int                             ng = mf.nGrow();

        const Box&                      domain = amr->getLevel(lev).Domain();

        for (MFIter mfi(mf); mfi.isValid(); ++mfi) // Loop over grids
        {
            // This is the valid Box of the current FArrayBox.
            // By "valid", we mean the original ungrown Box in BoxArray.
            const Box& box = mfi.validbox();

            // A reference to the current FArrayBox in this loop iteration.
            const FArrayBox& fab = mf[mfi];

            // Pointer to the floating point data of this FArrayBox.
            const Real* a = fab.dataPtr();

            // This is the Box on which the FArrayBox is defined.
            // Note that "abox" includes ghost cells (if there are any),
            // and is thus larger than or equal to "box".
            const Box& abox = fab.box();

            int gid = gid_offsets[lev] + mfi.index();
            Block::Shape hi = abox.hiVect(), lo = abox.loVect();
            Block::Shape shape = hi - lo + Block::Shape::one();

            diy::AMRLink* link = new diy::AMRLink(3, lev, refinements[lev], bounds(box), bounds(abox));
            master.add(gid, new Block(a, shape), link);

            // record wrap
            for (int dir_x : { -1, 0, 1 })
            {
                if (!is_periodic[0] && dir_x) continue;
                if (dir_x < 0 && box.loVect()[0] != domain.loVect()[0]) continue;
                if (dir_x > 0 && box.hiVect()[0] != domain.hiVect()[0]) continue;

                for (int dir_y : { -1, 0, 1 })
                {
                    if (!is_periodic[1] && dir_y) continue;
                    if (dir_y < 0 && box.loVect()[1] != domain.loVect()[1]) continue;
                    if (dir_y > 0 && box.hiVect()[1] != domain.hiVect()[1]) continue;

                    for (int dir_z : { -1, 0, 1 })
                    {
                        if (dir_x == 0 && dir_y == 0 && dir_z == 0)
                            continue;

                        if (!is_periodic[2] && dir_z) continue;
                        if (dir_z < 0 && box.loVect()[2] != domain.loVect()[2]) continue;
                        if (dir_z > 0 && box.hiVect()[2] != domain.hiVect()[2]) continue;

                        link->add_wrap(diy::Direction { dir_x, dir_y, dir_z });
                    }
                }
            }

            // record neighbors
            for (int nbr_lev = (std::max)(0, lev-1); nbr_lev <= (std::min)(finest_level, lev+1); ++nbr_lev)
            {
                // gotta do this yoga to work around AMReX's static variables
                const Box& nbr_lev_domain  = amr->getLevel(nbr_lev).Domain();
                Periodicity periodicity(IntVect(AMREX_D_DECL(nbr_lev_domain.length(0) * is_periodic[0],
                                                             nbr_lev_domain.length(1) * is_periodic[1],
                                                             nbr_lev_domain.length(2) * is_periodic[2])));

                const std::vector<IntVect>& pshifts = periodicity.shiftIntVect();
                const MultiFab&             mf = amr->getLevel(nbr_lev).get_old_data(PhiGrav_Type);       // TODO: might want a different way to specify the data type we want
                const BoxArray&             ba = mf.boxArray();
                int                         ng = mf.nGrow();

                Box gbx = box;
                if (nbr_lev < lev)
                    gbx.coarsen(amr->getLevel(nbr_lev).fineRatio());
                else if (nbr_lev > lev)
                    gbx.refine(amr->getLevel(lev).fineRatio());
                gbx.grow(1);

                for (const auto& piv : pshifts)
                {
                    ba.intersections(gbx + piv, isects);
                    for (const auto& is : isects)
                    {
                        // is.first is the index of neighbor box
                        // ba[is.first] is the neighbor box
                        int         nbr_gid         = gid_offsets[nbr_lev] + is.first;
                        const Box&  nbr_box         = ba[is.first];
                        Box         nbr_ghost_box   = grow(nbr_box,ng);

                        link->add_neighbor(diy::BlockID { nbr_gid, -1 });        // we don't know the proc, but we'll figure it out later through DynamicAssigner
                        link->add_bounds(nbr_lev, refinements[nbr_lev], bounds(nbr_box), bounds(nbr_ghost_box));
                    }
                }
            }
        }
    }


    // fill dynamic assigner and fix links
    diy::DynamicAssigner assigner(master.communicator(), master.communicator().size(), nblocks);
    diy::fix_links(master, assigner);

    henson_save_pointer("diy-amr",  &master);
    henson_save_pointer("assigner", &assigner);

    master.foreach([](Block* b, const diy::Master::ProxyWithLink& cp)
                   {
                     auto* l = static_cast<diy::AMRLink*>(cp.link());
                     fmt::print("{}: level = {}, shape = {}, core = {} - {}, bounds = {} - {}\n",
                                cp.gid(), l->level(), b->fab.shape(),
                                l->core().min, l->core().max,
                                l->bounds().min, l->bounds().max);
                   });

#if 0
    diy::MemoryBuffer header;
    diy::save(header, bounds(domain));
    diy::io::write_blocks("out", world, master, header);
#endif

    fmt::print("Done with diy-amr\n");
}
