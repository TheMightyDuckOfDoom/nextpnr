/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2023  gatecat <gatecat@ds0.me>
 *  Copyright (C) 2024  TheMightyDuckOfDoom <git@tsenti.li>
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

#include "himbaechel_api.h"
#include "log.h"
#include "nextpnr.h"
#include "util.h"

#include "himbaechel_helpers.h"

#define GEN_INIT_CONSTIDS
#define HIMBAECHEL_CONSTIDS "uarch/pcbfpga/constids.inc"
#include "himbaechel_constids.h"

NEXTPNR_NAMESPACE_BEGIN

namespace {
struct PCBFPGAImpl : HimbaechelAPI
{
    ~PCBFPGAImpl(){};
    void init_database(Arch *arch) override
    {
        init_uarch_constids(arch);
        arch->load_chipdb("pcbfpga/chipdb-test.bin");
        arch->set_speed_grade("DEFAULT");
    }

    void init(Context *ctx) override
    {
        h.init(ctx);
        HimbaechelAPI::init(ctx);
    }

    void prePlace() override { assign_cell_info(); }

    void pack() override
    {
        // Trim nextpnr IOBs - assume IO buffer insertion has been done in synthesis
        const pool<CellTypePort> top_ports{
                CellTypePort(id_INBUF, id_PAD),
                CellTypePort(id_OUTBUF, id_PAD),
        };
        h.remove_nextpnr_iobs(top_ports);
        // Replace constants with LUTs
        const dict<IdString, Property> vcc_params = {{id_INIT, Property(0xFFFF, 16)}};
        const dict<IdString, Property> gnd_params = {{id_INIT, Property(0x0000, 16)}};
        h.replace_constants(CellTypePort(id_VCC_DRV, id_VCC), CellTypePort(id_GND_DRV, id_GND), {}, {}, id_VCC, id_GND);
        // Constrain directly connected LUTs and FFs together to use dedicated resources
        int lutffs = h.constrain_cell_pairs(pool<CellTypePort>{{id_LUT4, id_F}}, pool<CellTypePort>{{id_DFF, id_D}}, 1);
        log_info("Constrained %d LUTFF pairs.\n", lutffs);
    }

    bool isBelLocationValid(BelId bel, bool explain_invalid) const override
    {
        Loc l = ctx->getBelLocation(bel);
        if (ctx->getBelType(bel).in(id_LUT4, id_DFF)) {
            return slice_valid(l.x, l.y, l.z / 2);
        } else {
            return true;
        }
    }

    // Bel bucket functions
    IdString getBelBucketForCellType(IdString cell_type) const override
    {
        if (cell_type.in(id_INBUF, id_OUTBUF))
            return id_IOB;
        return cell_type;
    }
    bool isValidBelForCellType(IdString cell_type, BelId bel) const override
    {
        IdString bel_type = ctx->getBelType(bel);
        if (bel_type == id_IOB)
            return cell_type.in(id_INBUF, id_OUTBUF);
        else
            return (bel_type == cell_type);
    }

  private:
    HimbaechelHelpers h;

    // Validity checking
    struct PCBFPGACellInfo
    {
        bool lut_i4_used = false;
    };
    std::vector<PCBFPGACellInfo> fast_cell_info;
    void assign_cell_info()
    {
        fast_cell_info.resize(ctx->cells.size());
    }
    bool slice_valid(int x, int y, int z) const
    {
        return true;
    }
};

struct PCBFPGAArch : HimbaechelArch
{
    PCBFPGAArch() : HimbaechelArch("pcbfpga"){};
    bool match_device(const std::string &device) override { return device == "test"; }
    std::unique_ptr<HimbaechelAPI> create(const std::string &device, const dict<std::string, std::string> &args)
    {
        return std::make_unique<PCBFPGAImpl>();
    }
} pcbfpgaArch;
} // namespace

NEXTPNR_NAMESPACE_END
