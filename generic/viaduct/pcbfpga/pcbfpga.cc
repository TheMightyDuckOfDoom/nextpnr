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

        mesh.init(ctx, &h, clbs_x, clbs_y, print_pips);
        mesh.build();
    }

    void setArgs(const dict<std::string, std::string> &args) {
        bool clbs_set = false;
        bool cluster_dffs_set = false;
        for(auto arg : args) {
            //log_info("PCBFPGAImpl: %s = %s\n", arg.first.c_str(), arg.second.c_str());
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
            } else if (arg.first == "cluster_dffs") {
                auto cfg = arg.second;
                if(cfg == "true") {
                    cluster_dffs = true;
                    cluster_dffs_set = true;
                    log_info("PCBFPGAImpl: cluster_dffs = true\n");
                } else if(cfg == "false") {
                    cluster_dffs = false;
                    cluster_dffs_set = true;
                    log_info("PCBFPGAImpl: cluster_dffs = false\n");
                } else {
                    log_error("PCBFPGAImpl: cluster_dffs argument should be true or false\n");
                }
            } else if (arg.first == "print_pips") {
                auto cfg = arg.second;
                if(cfg == "true") {
                    print_pips = true;
                    log_info("PCBFPGAImpl: print_pips = true\n");
                } else if(cfg == "false") {
                    print_pips = false;
                    log_info("PCBFPGAImpl: print_pips = false\n");
                } else {
                    log_error("PCBFPGAImpl: print_pips argument should be true or false\n");
                }
            }
        }

        if(!clbs_set) {
            log_info("PCBFPGAImpl: clbs not set, using default %ldx%ld\n", clbs_x, clbs_y);
        }

        if(!cluster_dffs_set) {
            log_info("PCBFPGAImpl: cluster_dffs not set, using default %s\n", cluster_dffs ? "true" : "false");
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
        const dict<IdString, Property> vcc_params = {{id_INIT, Property(0xFFFF, 16)}, {id_K, Property(0)}};
        const dict<IdString, Property> gnd_params = {{id_INIT, Property(0x0000, 16)}, {id_K, Property(0)}};
        h.replace_constants(CellTypePort(id_LUT, id_F), CellTypePort(id_LUT, id_F), vcc_params, gnd_params);

        // Pack DFFs
        std::map<NetInfo*, std::map<NetInfo*, std::map<NetInfo*, std::vector<CellInfo*>>>> dffs;
        // dffs[clk_net][rst_net][en_net] = {dff1, dff2, ...}

        // Create map of DFFs by clock, enable, and reset
        for(auto &ci : ctx->cells) {
            CellInfo *cell = ci.second.get();
            if(cell->type == id_DFF) {
                NetInfo *clk = cell->getPort(id_CLK);
                NetInfo *en = cell->getPort(id_EN);
                NetInfo *rst = cell->getPort(id_RST_N);

                // Check if DFF has clock
                if(clk == nullptr) {
                    log_error("DFF %s has no clock\n", cell->name.c_str(ctx));
                }

                dffs[clk][rst][en].push_back(cell);
            }
        }

        // Simple DFF packing, place DFFs in the same slice if they share the same clock, enable, and reset
        if(cluster_dffs) {
            size_t constrained_dff_slices = 0;
            for(auto &clk : dffs) {
                // CLK must be present
                assert(clk.first != nullptr);
                log_info("CLK %s\n", clk.first->name.c_str(ctx));
                for(auto &rst : clk.second) {
                    // Reset can be nullptr
                    log_info("  RST %s\n", rst.first == nullptr ? "NONE" : rst.first->name.c_str(ctx));
                    for(auto &en : rst.second) {
                        // Do not pack DFFs with no enable and reset -> can be placed anywhere with the same clock
                        if(rst.first == nullptr && en.first == nullptr) {
                            continue;
                        }

                        log_info("    EN %s\n", en.first == nullptr ? "NONE" : en.first->name.c_str(ctx));

                        // Check if there are even multiple DFFs to pack
                        if(en.second.size() < 2) {
                            printf("            No DFFs to pack, only %ld DFF\n", en.second.size());
                            continue;
                        }

                        // Constrain DFFs in SLICES_PER_CLB chunks
                        size_t dff_in_slice = 0;
                        size_t slice_count = 0;
                        CellInfo *first_dff = nullptr;
                        for(auto &dff : en.second) {
                            // Check that cell is not already constrained
                            if(dff->cluster != ClusterId()) {
                                log_error("DFF %s is already constrained\n", dff->name.c_str(ctx));
                            }

                            // Debug info
                            if(dff_in_slice == 0) {
                                constrained_dff_slices++;
                                if(slice_count > 0) 
                                    printf("\n");
                                printf("            SLICE %ld: ", slice_count);
                            }
                            printf("%s ", dff->name.c_str(ctx));

                            // Constrain DFF
                            if(dff_in_slice == 0) {
                                dff->cluster = dff->name;
                                dff->constr_abs_z = true;
                                dff->constr_x = 0;
                                dff->constr_y = 0;
                                dff->constr_z = 1;

                                first_dff = dff;
                            } else {
                                dff->cluster = first_dff->name;
                                dff->constr_abs_z = true;
                                dff->constr_x = 0;
                                dff->constr_y = 0;
                                dff->constr_z = dff_in_slice * 2 + 1;

                                first_dff->constr_children.push_back(dff);
                            }

                            dff_in_slice++;
                            if(dff_in_slice == SLICES_PER_CLB) {
                                dff_in_slice = 0;
                                slice_count++;
                            }
                        }
                        printf("\n");
                    }
                }
            }

            log_info("Constrained %ld slices with DFFs\n", constrained_dff_slices);
        }

        // Pack LUTs
        size_t lut_count[LUT_INPUTS];
        memset(lut_count, 0, sizeof(lut_count));

        size_t lutdff_pairs = 0;
        for(auto &ci : ctx->cells) {
            CellInfo *cell = ci.second.get();
            if(cell->type == id_LUT) {
                size_t k = cell->params[id_K].as_int64();
                if(k > LUT_INPUTS) {
                    log_error("LUT at %s has K=%ld, but only %ld inputs available\n", cell->name.c_str(ctx), k, LUT_INPUTS);
                } 
                lut_count[k - 1]++;

                // Handle K=1 LUTs, need to rename I to I[0]
                if(k == 1) {
                    cell->renamePort(id_I, ctx->id("I[0]"));
                }

                // TODO: Handle constant inputs -> replicate inputs

                // Check if LUT is already constrained
                if(cell->cluster != ClusterId()) {
                    log_info("LUT %s is already constrained\n", cell->name.c_str(ctx));
                    continue;
                }

                // Check if LUT drives a DFF D input
                NetInfo *lut_out = cell->getPort(id_F);
                std::vector<CellInfo*> dff_users;
                for(auto &user : lut_out->users) {
                    if(user.cell->type == id_DFF && user.port == id_D) {
                        dff_users.push_back(user.cell);
                    }
                }

                if(dff_users.size() > 0) {
                    if(dff_users.size() > 1) {
                        // Normally this should not happen, as it would mean multiple DFFs register the same value
                        // (if the have the same clock, enable and reset)
                        log_warning("LUT %s drives multiple DFF D inputs\n", cell->name.c_str(ctx));
                        for(auto &dff : dff_users) {
                            NetInfo* dff_clk = dff->getPort(id_CLK);
                            NetInfo* dff_rst = dff->getPort(id_RST_N);
                            NetInfo* dff_en = dff->getPort(id_EN);
                            log_warning("    DFF: %s CLK: %s RST_N: %s EN: %s\n", dff->name.c_str(ctx), dff_clk == nullptr ? "NONE" : dff_clk->name.c_str(ctx), dff_rst == nullptr ? "NONE" : dff_rst->name.c_str(ctx), dff_en == nullptr ? "NONE" : dff_en->name.c_str(ctx));
                        }
                    }

                    CellInfo *dff = dff_users.at(0);
                    // Check if the DFF is already constrained
                    if(dff->cluster != ClusterId()) {
                        // Constrain this LUT to the same cluster
                        log_info("LUT %s drives constrained DFF %s in cluster %s\n", cell->name.c_str(ctx), dff->name.c_str(ctx), dff->cluster.c_str(ctx));

                        // DFF must be constrained absolute
                        if(!dff->constr_abs_z) {
                            log_error("LUT %s is not constrained absolute\n", dff->name.c_str(ctx));
                        }

                        // Constrain the LUT absolute
                        cell->cluster = dff->cluster;
                        cell->constr_abs_z = true;
                        cell->constr_x = 0;
                        cell->constr_y = 0;
                        cell->constr_z = dff->constr_z - 1;

                        // Add LUT to DFF cluster
                        ctx->cells[dff->cluster]->constr_children.push_back(cell);
                    } else {
                        // Constrain the LUT relative
                        cell->cluster = cell->name;
                        cell->constr_abs_z = false;
                        
                        // Add the DFF to the cluster
                        cell->constr_children.push_back(dff);

                        // Constrain the DFF relative
                        dff->cluster = cell->name;
                        dff->constr_abs_z = false;
                        dff->constr_x = 0;
                        dff->constr_y = 0;
                        dff->constr_z = 1;

                        log_info("LUT %s drives DFF %s, creating new cluster\n", cell->name.c_str(ctx), dff->name.c_str(ctx));
                    }
                    lutdff_pairs++;
                }
            }
        }

        for(size_t i = 0; i < LUT_INPUTS; i++) {
            log_info("LUTs with %ld inputs: %ld\n", i + 1, lut_count[i]);
        }

        log_info("Constrained %ld LUTs to drive DFF D inputs\n", lutdff_pairs);
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
    bool cluster_dffs = true;
    bool print_pips = false;

    typedef struct {
        const NetInfo *dff_clk = nullptr;
        const NetInfo *dff_en = nullptr;
        const NetInfo *dff_rst = nullptr;
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
                fc.dff_en = ci->getPort(id_EN);
                fc.dff_rst = ci->getPort(id_RST_N);
            }
        }
    }

    bool slice_valid(int x, int y, int z) const
    {
        // Check that all DFFs in the slice are clocked by the same net
        const NetInfo *clk_net = nullptr;
        const NetInfo *en_net = nullptr;
        const NetInfo *rst_net = nullptr;
        for(size_t slice = 0; slice < SLICES_PER_CLB; slice++) {
            const CellInfo *dff = ctx->getBoundBelCell(ctx->getBelByLocation(Loc(x, y, slice * 2 + 1)));

            // DFF not bound
            if(dff == nullptr) {
                continue;
            }

            const auto &dff_data = cell_info.at(dff->flat_index);
            
            // Check if DFF has clock
            if(dff_data.dff_clk == nullptr) {
                log_error("Slice at (%d, %d) has DFF without clock\n", x, y);
                return false;
            }

            // Check clock
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

            // Check enable
            if(en_net == nullptr) {
                en_net = dff_data.dff_en;
            } else if(en_net != dff_data.dff_en) {
                // Check if the enable net is the same for all DFFs in the slice
                return false;
            }

            // Check reset
            if(rst_net == nullptr) {
                rst_net = dff_data.dff_rst;
            } else if(rst_net != dff_data.dff_rst) {
                // Check if the reset net is the same for all DFFs in the slice
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
