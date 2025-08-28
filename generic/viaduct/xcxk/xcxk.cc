/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2021  gatecat <gatecat@ds0.me>
 *  Copyright (C) 2024  Tobias Senti <git@tsenti.li>
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

#include "log.h"
#include "nextpnr.h"
#include "util.h"
#include "viaduct_api.h"
#include "viaduct_helpers.h"

#define GEN_INIT_CONSTIDS
#define VIADUCT_CONSTIDS "viaduct/xcxk/constids.inc"
#include "viaduct_constids.h"

#include "xc3000.h"

NEXTPNR_NAMESPACE_BEGIN

namespace {
struct XCxkImpl : ViaductAPI
{
    ~XCxkImpl() {};
    void init(Context *ctx) override
    {
        init_uarch_constids(ctx);
        ViaductAPI::init(ctx);
        h.init(ctx);

        device.init_device(ctx, &h, device_name, with_gui);
    }

    void pack() override {
        // Trim nextpnr IOBs - assume IO buffer insertion has been done in synthesis
        const pool<CellTypePort> top_ports{
            CellTypePort(id_IBUF, id_I),
            CellTypePort(id_OBUF, id_O),
        };
        h.remove_nextpnr_iobs(top_ports);

        // Pack IOBs and LUTs
        for (auto &cell : ctx->cells) {
            auto &ci = *cell.second;
            if(ci.type == id_IBUF) {
                log_info("Converting IBUF %s to IOB\n", ci.name.c_str(ctx));
                // Change type
                ci.type = id_IOB;
                // Swap I with PAD
                // Swap O with I
                ci.renamePort(id_I, id_PAD);
                ci.renamePort(id_O, id_I);

                if(no_iob_routing) {
                    ci.disconnectPort(id_I);
                    ci.disconnectPort(id_PAD);
                }
            } else if(ci.type == id_OBUF) {
                log_info("Converting OBUF %s to IOB\n", ci.name.c_str(ctx));
                // Change type
                ci.type = id_IOB;
                // Swap O with PAD
                // Swap I with O
                ci.renamePort(id_O, id_PAD);
                ci.renamePort(id_I, id_O);

                if(no_iob_routing) {
                    ci.disconnectPort(id_O);
                    ci.disconnectPort(id_PAD);
                }
            } else if(ci.type == id_LUT) {
                log_info("Converting LUT %s to LUT5\n", ci.name.c_str(ctx));
                size_t lut_k = ci.params[ctx->id("K")].as_int64();
                log_info("LUT K = %ld\n", lut_k);

                // TODO: Implement fractionable LUTs
                ci.type = id_LUT5;

                ci.renamePort(id_O, id_F);
            } else if(ci.type == id_DFF) {
                ci.renamePort(id_CE, id_EC);
            }
        }
    }

    void prePlace() override {
        assign_cell_info();
    }

    bool isBelLocationValid(BelId bel, bool explain_invalid) const override {
        if(ctx->getBelType(bel) == id_DFF) {
            return isCLBValid(bel);
        }
        return true;
    }

    void setArgs(const dict<std::string, std::string> &args) {
        for(auto arg : args) {
            log_info("Got argument %s = %s\n", arg.first.c_str(), arg.second.c_str());
            if(arg.first == "device") {
                device_name = arg.second;
            } else if (arg.first == "no_iob_routing") {
                no_iob_routing = true;
            }
        }
    }

  private:
    ViaductHelpers h;
    std::string device_name = "3090";
    bool no_iob_routing = false;

    xc3000 device;

    typedef struct {
        const NetInfo *ff_ec = nullptr;
        const NetInfo *ff_c = nullptr;
        const NetInfo *ff_rd = nullptr;
    } cell_info_t;
    std::vector<cell_info_t> cell_info;

    void assign_cell_info() {
        cell_info.resize(ctx->cells.size());
        for(auto &cell : ctx->cells) {
            CellInfo *ci = cell.second.get();
            auto &info = cell_info.at(ci->flat_index);
            if(ci->type == id_DFF) {
                info.ff_ec = ci->getPort(id_EC);
                info.ff_c = ci->getPort(id_C);
                info.ff_rd = ci->getPort(id_RD);
            }
        }
    }

    bool isCLBValid(BelId bel) const {
        Loc l = ctx->getBelLocation(bel);

        // Check if all FFs in CLB use the same clock, clock enabled and reset
        const CellInfo *qx = ctx->getBoundBelCell(ctx->getBelByLocation(Loc(l.x, l.y, QX_Z)));
        const CellInfo *qy = ctx->getBoundBelCell(ctx->getBelByLocation(Loc(l.x, l.y, QY_Z)));

        if(qx != nullptr && qy != nullptr) {
            auto &info_qx = cell_info.at(qx->flat_index);
            auto &info_qy = cell_info.at(qy->flat_index);

            if(info_qx.ff_c == nullptr) {
                log_error("QX at (%2d, %2d) has no clock\n", l.x, l.y);
                return false;
            }
            if(info_qy.ff_c == nullptr) {
                log_error("QY at (%2d, %2d) has no clock\n", l.x, l.y);
                return false;
            }

            if(info_qx.ff_c != info_qy.ff_c)
                return false;

            if(info_qx.ff_ec != info_qy.ff_ec)
                return false;

            if(info_qx.ff_rd != info_qy.ff_rd)
                return false;
        }

        return true;
    }
};

struct XCxkArch : ViaductArch
{
    XCxkArch() : ViaductArch("xcxk") {};
    std::unique_ptr<ViaductAPI> create(const dict<std::string, std::string> &args)
    {
        auto ptr = std::make_unique<XCxkImpl>();
        ptr->setArgs(args);
        return ptr;
    }
} xcxkArch;
} // namespace

NEXTPNR_NAMESPACE_END
