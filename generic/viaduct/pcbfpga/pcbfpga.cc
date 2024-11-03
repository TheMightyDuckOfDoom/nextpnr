/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2021  gatecat <gatecat@ds0.me>
 *
 *  Permission to use, copy, modify, and/or distribute this software for any
 *  purpose with or without fee is hereby granted, provided that the above
 *  copyright notice and this permission notice appear in all copies.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 *  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 *  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 *  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 *  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

// Tobias Senti October 2024

#include "log.h"
#include "nextpnr.h"
#include "util.h"
#include "viaduct_api.h"
#include "viaduct_helpers.h"

#define GEN_INIT_CONSTIDS
#define VIADUCT_CONSTIDS "viaduct/pcbfpga/constids.inc"
#include "viaduct_constids.h"

#include "mesh.h"

NEXTPNR_NAMESPACE_BEGIN

namespace {
struct PCBFPGAImpl : ViaductAPI
{
    ~PCBFPGAImpl() {};
    void init(Context *ctx) override
    {
        init_uarch_constids(ctx);
        ViaductAPI::init(ctx);
        h.init(ctx);

        assert(args_set);

        mesh.init(ctx, &h, clbs_x, clbs_y);
        mesh.build();
    }

    void setArgs(const dict<std::string, std::string> &args) {
        bool clbs_set = false;
        for(auto arg : args) {
            log_info("PCBFPGAImpl: %s = %s\n", arg.first.c_str(), arg.second.c_str());
            if(arg.first == "clbs") {
                auto cfg = arg.second;
                if(cfg.find("x") != std::string::npos) {
                    clbs_x = std::stoi(cfg.substr(0, cfg.find("x")));
                    clbs_y = std::stoi(cfg.substr(cfg.find("x") + 1));

                    log_info("PCBFPGAImpl: clbs_x = %ld, clbs_y = %ld\n", clbs_x, clbs_y);
                    clbs_set = true;
                } else {
                    log_error("PCBFPGAImpl: clbs argument should have format NxM, where N,M integers\n");
                }
            }
        }

        if(!clbs_set) {
            log_info("PCBFPGAImpl: clbs not set, using default %ldx%ld\n", clbs_x, clbs_y);
        }

        args_set = true;
    }

    void pack() override
    {
        // Trim nextpnr IOBs - assume IO buffer insertion has been done in synthesis
        const pool<CellTypePort> top_ports{
                CellTypePort(id_IBUF, id_PAD),
                CellTypePort(id_OBUF, id_PAD),
        };
        h.remove_nextpnr_iobs(top_ports);
        // Replace constants with LUTs
        const dict<IdString, Property> vcc_params = {{id_INIT, Property(0xFFFF, 16)}};
        const dict<IdString, Property> gnd_params = {{id_INIT, Property(0x0000, 16)}};
        h.replace_constants(CellTypePort(id_LUT, id_F), CellTypePort(id_LUT, id_F), vcc_params, gnd_params);
        // Constrain directly connected LUTs and FFs together to use dedicated resources
        int lutffs = h.constrain_cell_pairs(pool<CellTypePort>{{id_LUT, id_F}}, pool<CellTypePort>{{id_DFF, id_D}}, 1);
        log_info("Constrained %d LUTFF pairs.\n", lutffs);
    }

    void prePlace() override {
        assign_cell_info();
        mesh.update_timing();
    }

    bool isBelLocationValid(BelId bel, bool explain_invalid) const override
    {
        Loc l = ctx->getBelLocation(bel);
        if (ctx->getBelType(bel).in(id_LUT, id_DFF)) {
            return slice_valid(l.x, l.y, l.z / 2);
        } else {
            return true;
        }
    }

    IdString getBelBucketForCellType(IdString cell_type) const override
    {
        if(cell_type.in(id_IBUF, id_OBUF))
            return id_IOB;
        return cell_type;
    }

    bool isValidBelForCellType(IdString cell_type, BelId bel) const override
    {
        IdString bel_type = ctx->getBelType(bel);
        if(bel_type == id_IOB)
            return cell_type.in(id_IBUF, id_OBUF);
        return bel_type == cell_type;
    }

  private:
    Mesh mesh;

    ViaductHelpers h;
    bool args_set = false;
    size_t clbs_x = 2;
    size_t clbs_y = clbs_x;

    typedef struct {
        const NetInfo *dff_clk = nullptr;
    } cell_info_t;
    std::vector<cell_info_t> cell_info;

    void assign_cell_info()
    {
        cell_info.resize(ctx->cells.size());
        for (auto &cell : ctx->cells) {
            CellInfo *ci = cell.second.get();
            auto &fc = cell_info.at(ci->flat_index);
            if (ci->type == id_DFF) {
                fc.dff_clk = ci->getPort(id_CLK);
            }
        }
    }

    bool slice_valid(int x, int y, int z) const
    {
        // Check that all DFFs in the slice are clocked by the same net
        const NetInfo *clk_net = nullptr;
        for(size_t slice = 0; slice < mesh.SLICES_PER_CLB; slice++) {
            const CellInfo *dff = ctx->getBoundBelCell(ctx->getBelByLocation(Loc(x, y, slice * 2 + 1)));

            if(dff == nullptr) {
                continue;
            }

            const auto &dff_data = cell_info.at(dff->flat_index);
            if(dff_data.dff_clk == nullptr) {
                log_error("Slice at (%d, %d) has DFF without clock\n", x, y);
                return false;
            }

            if(clk_net == nullptr) {
                // Set the clock net for the whole CLB
                clk_net = dff_data.dff_clk;
                //log_info("Slice at (%d, %d, %ld) has clock net %s\n", x, y, slice, clk_net->name.c_str(ctx));
            } else if(clk_net != dff_data.dff_clk) {
                // Check if the clock net is the same for all DFFs in the slice
                //log_warning("Slice at (%d, %d, %ld) has DFFs with different clocks\n", x, y, slice);
                //log_info("    %s vs %s\n", clk_net->name.c_str(ctx), dff_data.dff_clk->name.c_str(ctx));
                return false;
            }
        }

        return true;
    }
};

struct PCBFPGAArch : ViaductArch
{
    PCBFPGAArch() : ViaductArch("pcbfpga") {};
    std::unique_ptr<ViaductAPI> create(const dict<std::string, std::string> &args)
    {
        auto impl = std::make_unique<PCBFPGAImpl>();
        impl->setArgs(args);
        return impl;
    }
} pcbfpgaArch;
} // namespace

NEXTPNR_NAMESPACE_END
