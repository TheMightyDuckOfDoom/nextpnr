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
    }

    void prePlace() override {}

    void setArgs(const dict<std::string, std::string> &args) {
        for(auto arg : args) {
            log_info("Got argument %s = %s\n", arg.first.c_str(), arg.second.c_str());
            if(arg.first == "device") {
                device_name = arg.second;
            }
        }
    }

  private:
    ViaductHelpers h;
    std::string device_name = "3090";

    xc3000 device;

    IdString getBelBucketForCellType(IdString cell_type) const override
    {
        if (cell_type.in(id_IBUF, id_OBUF))
            return id_IOB;
        return cell_type;
    }

    bool isValidBelForCellType(IdString cell_type, BelId bel) const override
    {
        IdString bel_type = ctx->getBelType(bel);
        if (bel_type == id_IOB)
            return cell_type.in(id_IBUF, id_OBUF);
        else
            return (bel_type == cell_type);
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
