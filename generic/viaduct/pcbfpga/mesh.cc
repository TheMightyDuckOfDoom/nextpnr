#include "mesh.h"
#include "log.h"

#define VIADUCT_CONSTIDS "viaduct/pcbfpga/constids.inc"
#include "viaduct_constids.h"

NEXTPNR_NAMESPACE_BEGIN

const char* tile_type_to_string(tile_type_t type) {
    switch(type) {
        case TILE_NONE: return "   ";
        case TILE_IOB: return "IOB";
        case TILE_CLB: return "CLB";
        case TILE_QSB: return "qsb";
        case TILE_QCB: return "qcb";
        case TILE_COR: return "cor";
    }
    return "UKN";
}

// Helper functions to determine tile types
bool is_perimeter(size_t x, size_t y, size_t DIM_X, size_t DIM_Y) {
    return x == 0 || x == DIM_X - 1 || y == 0 || y == DIM_Y - 1;
}

bool is_corner(size_t x, size_t y, size_t DIM_X, size_t DIM_Y) {
    return (x == 0 && y == 0) || (x == DIM_X - 1 && y == 0) || (x == 0 && y == DIM_Y - 1) || (x == DIM_X - 1 && y == DIM_Y - 1);
}

bool is_secondary_corner(size_t x, size_t y, size_t DIM_X, size_t DIM_Y) {
    return (x == 1 && y == 1) || (x == DIM_X - 2 && y == 1) || (x == 1 && y == DIM_Y - 2) || (x == DIM_X - 2 && y == DIM_Y - 2);
}

bool is_io(size_t x, size_t y, size_t DIM_X, size_t DIM_Y) {
    return is_perimeter(x, y, DIM_X, DIM_Y) && !is_corner(x, y, DIM_X, DIM_Y) && ((x + y) % 2 == 0);
}

bool is_clb(size_t x, size_t y, size_t DIM_X, size_t DIM_Y) {
    return !is_perimeter(x, y, DIM_X, DIM_Y) && (x % 2 == 0) && (y % 2 == 0);
}

bool is_qsb(size_t x, size_t y, size_t DIM_X, size_t DIM_Y) {
    return !is_perimeter(x, y, DIM_X, DIM_Y) && (x % 2 == 1) && (y % 2 == 1);
}

void Mesh::init(Context *ctx, ViaductHelpers *h, size_t CLBS_X, size_t CLBS_Y, bool print_pips) {
    this->ctx = ctx;
    this->h = h;
    this->CLBS_X = CLBS_X;
    this->CLBS_Y = CLBS_Y;
    this->DIM_X = CLBS_X * 2 + 3;
    this->DIM_Y = CLBS_Y * 2 + 3;
    this->print_pips = print_pips;
}

void Mesh::build() {
    build_mesh();
    build_wires();
    build_pips();
    build_bels();

    if(print_pips) {
        for(auto pip : ctx->getPips()) {
            log_info("Pip %s %s -> %s\n", ctx->getPipName(pip).str(ctx).c_str(), ctx->getWireName(ctx->getPipSrcWire(pip)).str(ctx).c_str(), ctx->getWireName(ctx->getPipDstWire(pip)).str(ctx).c_str());
        }
    }

    print();

    // PCB estimate
    size_t num_clbs = CLBS_X * CLBS_Y;
    size_t num_qsbs = (CLBS_X - 1) * (CLBS_Y - 1);

    size_t mux8_per_clb = SLICES_PER_CLB * (2 + SLICE_INPUTS) + 2;

    size_t buf_per_qsb = CHANNEL_WIDTH * 6;

    log_info("CLBs use %ld mux8 each, %ld mux8 total\n", mux8_per_clb, num_clbs * mux8_per_clb);
    log_info("QSBs use %ld buffers each, %ld buffers total\n", buf_per_qsb, num_qsbs * buf_per_qsb);

    size_t qcb_mux8_per_clb = CLB_INPUTS_PER_SIDE * 2;
    log_info("QCBs use %ld mux8 each, %ld mux8 total\n", qcb_mux8_per_clb, num_clbs * qcb_mux8_per_clb);

    // Adjust timing estimates
    ctx->args.delayOffset = MUX8_DELAY;
    ctx->args.delayScale  = DUMMY_DELAY;
}

void Mesh::print() {
    log_info("    ");
    for(size_t x = 0; x < DIM_X; x++) {
        printf("%3ld ", x);
    }
    printf("\n");
    
    for(size_t y = 0; y < DIM_Y; y++) {
        log_info("%3ld ", y);
        for(size_t x = 0; x < mesh[y].size(); x++) {
            printf("%s ", tile_type_to_string(mesh[y][x]));
        }
        printf("\n");
    }
}

void Mesh::build_mesh() {
    mesh.resize(DIM_Y, std::vector<tile_type_t>(DIM_X, TILE_NONE));

    size_t count[6] = {0, 0, 0, 0, 0, 0};

    for(size_t y = 0; y < DIM_Y; y++) {
        for(size_t x = 0; x < DIM_X; x++) {
            if(is_io(x, y, DIM_X, DIM_Y))
                mesh[y][x] = TILE_IOB;
            else if(is_clb(x, y, DIM_X, DIM_Y))
                mesh[y][x] = TILE_CLB;
            else if(is_secondary_corner(x, y, DIM_X, DIM_Y))
                mesh[y][x] = TILE_COR;
            else if(is_qsb(x, y, DIM_X, DIM_Y))
                mesh[y][x] = TILE_QSB;
            else if(!is_perimeter(x, y, DIM_X, DIM_Y))
                mesh[y][x] = TILE_QCB;

            count[mesh[y][x]]++;
        }
    }
    log_info("Mesh built\n");
    for(size_t i = 1; i < 6; i++) {
        log_info("    %s: %ld\n", tile_type_to_string((tile_type_t)i), count[i]);
    }
}

wire_map_t Mesh::build_qcb_wires(size_t x, size_t y) {
    assert(mesh[y][x] == TILE_QCB);
    wire_map_t wire_map;

    std::vector<WireId> wires(CHANNEL_WIDTH);
    for(size_t i = 0; i < CHANNEL_WIDTH; i++) {
        wires[i] = ctx->addWire(h->xy_id(x, y, ctx->idf("CHANNEL%d", i)), ctx->id("CHANNEL"), x, y);
    }
    wire_map["CHANNEL"] = wires;

    return wire_map;
}

wire_map_t Mesh::build_clb_wires(size_t x, size_t y) {
    assert(mesh[y][x] == TILE_CLB);
    wire_map_t wire_map;

    // Input wires
    const char* inp_dirs[] = {"NORTH_IN", "EAST_IN", "SOUTH_IN", "WEST_IN"};
    for(auto dir : inp_dirs) {
        std::vector<WireId> wires(CLB_INPUTS_PER_SIDE);
        for(size_t i = 0; i < CLB_INPUTS_PER_SIDE; i++) {
            wires[i] = ctx->addWire(h->xy_id(x, y, ctx->idf("%s%d", dir, i)), ctx->id(dir), x, y);
        }
        wire_map[dir] = wires;
    }
    
    // Output wires
    const char* out_dirs[] = {"NORTH_OUT", "EAST_OUT", "SOUTH_OUT", "WEST_OUT"};
    for(auto dir : out_dirs) {
        std::vector<WireId> wires(CLB_OUTPUTS_PER_SIDE);
        for(size_t i = 0; i < CLB_OUTPUTS_PER_SIDE; i++) {
            wires[i] = ctx->addWire(h->xy_id(x, y, ctx->idf("%s%d", dir, i)), ctx->id(dir), x, y);
        }
        wire_map[dir] = wires;
    }

    /// Slice wires
    wire_map["SLICE_CLK"] = std::vector<WireId>(1, ctx->addWire(h->xy_id(x, y, ctx->idf("SLICE_CLK")), ctx->id("SLICE_CLK"), x, y));
    wire_map["SLICE_EN"] = std::vector<WireId>(1, ctx->addWire(h->xy_id(x, y, ctx->idf("SLICE_EN")), ctx->id("SLICE_EN"), x, y));
    wire_map["SLICE_RST_N"] = std::vector<WireId>(1, ctx->addWire(h->xy_id(x, y, ctx->idf("SLICE_RST_N")), ctx->id("SLICE_RST_N"), x, y));

    // Inputs
    wire_map["SLICE_IN"] = std::vector<WireId>(SLICES_PER_CLB * SLICE_INPUTS);
    for(size_t i = 0; i < SLICES_PER_CLB; i++) {
        for(size_t j = 0; j < LUT_INPUTS; j++) {
            wire_map["SLICE_IN"][i * SLICE_INPUTS + j] = ctx->addWire(h->xy_id(x, y, ctx->idf("SLICE%d_LUT%d", i, j)), ctx->id("SLICE_LUT"), x, y);
        }
        wire_map["SLICE_IN"][i * SLICE_INPUTS + LUT_INPUTS] = ctx->addWire(h->xy_id(x, y, ctx->idf("SLICE%d_D", i)), ctx->id("SLICE_D"), x, y);
    }

    // Outputs
    wire_map["SLICE_OUT"] = std::vector<WireId>(SLICES_PER_CLB * SLICE_OUTPUTS);
    for(size_t i = 0; i < SLICES_PER_CLB; i++) {
        wire_map["SLICE_OUT"][i * SLICE_OUTPUTS] = ctx->addWire(h->xy_id(x, y, ctx->idf("SLICE%d_F", i)), ctx->id("SLICE_F"), x, y);
        wire_map["SLICE_OUT"][i * SLICE_OUTPUTS + 1] = ctx->addWire(h->xy_id(x, y, ctx->idf("SLICE%d_Q", i)), ctx->id("SLICE_Q"), x, y);
    }

    return wire_map;
}

wire_map_t Mesh::build_iob_wires(size_t x, size_t y) {
    assert(mesh[y][x] == TILE_IOB);
    wire_map_t wire_map;

    // IO wires
    wire_map["IO_IN"] = std::vector<WireId>(IO_PER_IOB * 2);
    wire_map["IO_INOUT"] = std::vector<WireId>(IO_PER_IOB);
    wire_map["IO_OUT"] = std::vector<WireId>(IO_PER_IOB);
    for(size_t i = 0; i < IO_PER_IOB; i++) {
        wire_map["IO_IN"][i * 2] = ctx->addWire(h->xy_id(x, y, ctx->idf("IO%d_I", i)), id_I, x, y);
        wire_map["IO_IN"][i * 2 + 1] = ctx->addWire(h->xy_id(x, y, ctx->idf("IO%d_EN", i)), id_EN, x, y);
        wire_map["IO_INOUT"][i] = ctx->addWire(h->xy_id(x, y, ctx->idf("IO%d_PAD", i)), id_PAD, x, y);
        wire_map["IO_OUT"][i] = ctx->addWire(h->xy_id(x, y, ctx->idf("IO%d_O", i)), id_O, x, y);
    }

    return wire_map;
}

void Mesh::build_wires() {
    wire_mesh.resize(DIM_Y, std::vector<wire_map_t>(DIM_X));

    for(size_t y = 0; y < DIM_Y; y++) {
        for(size_t x = 0; x < DIM_X; x++) {
            switch(mesh[y][x]) {
                case TILE_QCB:
                    wire_mesh[y][x] = build_qcb_wires(x, y);
                    break;
                case TILE_CLB:
                    wire_mesh[y][x] = build_clb_wires(x, y);
                    break;
                case TILE_IOB:
                    wire_mesh[y][x] = build_iob_wires(x, y);
                    break;
                default:
                    break;
            }
        }
    }
}

void Mesh::build_corner_pips(size_t x, size_t y) {
    assert(is_secondary_corner(x, y, DIM_X, DIM_Y));

    // Top left corner
    if(x == 1 && y == 1) {
        // Connect bottom qcb to right qcb
        for(size_t i = 0; i < CHANNEL_WIDTH; i++) {
            WireId bottom = wire_mesh[y + 1][x]["CHANNEL"][i];
            WireId right = wire_mesh[y][x + 1]["CHANNEL"][i];
            ctx->addPip(h->xy_id(x, y, ctx->idf("DUMMY1_CHANNEL%d", i)), id_CORNERPIP, right, bottom, DUMMY_DELAY, Loc(x, y, 0));
            ctx->addPip(h->xy_id(x, y, ctx->idf("DUMMY2_CHANNEL%d", i)), id_CORNERPIP, bottom, right, DUMMY_DELAY, Loc(x, y, 0));
        }
    }

    // Top right corner
    if(x == DIM_X-2 && y == 1) {
        // Connect bottom qcb to left qcb
        for(size_t i = 0; i < CHANNEL_WIDTH; i++) {
            WireId bottom = wire_mesh[y + 1][x]["CHANNEL"][i];
            WireId left = wire_mesh[y][x - 1]["CHANNEL"][i];
            ctx->addPip(h->xy_id(x, y, ctx->idf("DUMMY1_CHANNEL%d", i)), id_CORNERPIP, left, bottom, DUMMY_DELAY, Loc(x, y, 0));
            ctx->addPip(h->xy_id(x, y, ctx->idf("DUMMY2_CHANNEL%d", i)), id_CORNERPIP, bottom, left, DUMMY_DELAY, Loc(x, y, 0));
        }
    }

    // Bottom left corner
    if(x == 1 && y == DIM_Y-2) {
        // Connect top qcb to right qcb
        for(size_t i = 0; i < CHANNEL_WIDTH; i++) {
            WireId top = wire_mesh[y - 1][x]["CHANNEL"][i];
            WireId right = wire_mesh[y][x + 1]["CHANNEL"][i];
            ctx->addPip(h->xy_id(x, y, ctx->idf("DUMMY1_CHANNEL%d", i)), id_CORNERPIP, right, top, DUMMY_DELAY, Loc(x, y, 0));
            ctx->addPip(h->xy_id(x, y, ctx->idf("DUMMY2_CHANNEL%d", i)), id_CORNERPIP, top, right, DUMMY_DELAY, Loc(x, y, 0));
        }
    }

    // Bottom right corner
    if(x == (DIM_X-2) && y == (DIM_Y-2)) {
        // Connect top qcb to left qcb
        for(size_t i = 0; i < CHANNEL_WIDTH; i++) {
            WireId top = wire_mesh[y - 1][x]["CHANNEL"][i];
            WireId left = wire_mesh[y][x - 1]["CHANNEL"][i];
            ctx->addPip(h->xy_id(x, y, ctx->idf("DUMMY1_CHANNEL%d", i)), id_CORNERPIP, left, top, DUMMY_DELAY, Loc(x, y, 0));
            ctx->addPip(h->xy_id(x, y, ctx->idf("DUMMY2_CHANNEL%d", i)), id_CORNERPIP, top, left, DUMMY_DELAY, Loc(x, y, 0));
        }
    }
}

void Mesh::build_qsb_pips(size_t x, size_t y) {
    // Make sure this tile is a QSB
    assert(is_qsb(x, y, DIM_X, DIM_Y));

    const double pip_delay = DUMMY_DELAY;

    for(size_t i = 0; i < CHANNEL_WIDTH; i++) {
        // Noth south
        if((mesh[y - 1][x] == TILE_QCB) && (mesh[y + 1][x] == TILE_QCB)) {
            ctx->addPip(h->xy_id(x, y, ctx->idf("NS_CHANNEL%d", i)), id_QSBPIP, wire_mesh[y - 1][x]["CHANNEL"][i], wire_mesh[y + 1][x]["CHANNEL"][i], pip_delay, Loc(x, y, 0));
            ctx->addPip(h->xy_id(x, y, ctx->idf("SN_CHANNEL%d", i)), id_QSBPIP, wire_mesh[y + 1][x]["CHANNEL"][i], wire_mesh[y - 1][x]["CHANNEL"][i], pip_delay, Loc(x, y, 0));
        }
        // East West
        if((mesh[y][x - 1] == TILE_QCB) && (mesh[y][x + 1] == TILE_QCB)) {
            ctx->addPip(h->xy_id(x, y, ctx->idf("EW_CHANNEL%d", i)), id_QSBPIP, wire_mesh[y][x - 1]["CHANNEL"][i], wire_mesh[y][x + 1]["CHANNEL"][i], pip_delay, Loc(x, y, 0));
            ctx->addPip(h->xy_id(x, y, ctx->idf("WE_CHANNEL%d", i)), id_QSBPIP, wire_mesh[y][x + 1]["CHANNEL"][i], wire_mesh[y][x - 1]["CHANNEL"][i], pip_delay, Loc(x, y, 0));
        }
        // North West
        if((mesh[y - 1][x] == TILE_QCB) && (mesh[y][x - 1] == TILE_QCB)) {
            ctx->addPip(h->xy_id(x, y, ctx->idf("NW_CHANNEL%d", i)), id_QSBPIP, wire_mesh[y - 1][x]["CHANNEL"][i], wire_mesh[y][x - 1]["CHANNEL"][i], pip_delay, Loc(x, y, 0));
            ctx->addPip(h->xy_id(x, y, ctx->idf("WN_CHANNEL%d", i)), id_QSBPIP, wire_mesh[y][x - 1]["CHANNEL"][i], wire_mesh[y - 1][x]["CHANNEL"][i], pip_delay, Loc(x, y, 0));
        }
        // South East
        if((mesh[y + 1][x] == TILE_QCB) && (mesh[y][x + 1] == TILE_QCB)) {
            ctx->addPip(h->xy_id(x, y, ctx->idf("SE_CHANNEL%d", i)), id_QSBPIP, wire_mesh[y + 1][x]["CHANNEL"][i], wire_mesh[y][x + 1]["CHANNEL"][i], pip_delay, Loc(x, y, 0));
            ctx->addPip(h->xy_id(x, y, ctx->idf("ES_CHANNEL%d", i)), id_QSBPIP, wire_mesh[y][x + 1]["CHANNEL"][i], wire_mesh[y + 1][x]["CHANNEL"][i], pip_delay, Loc(x, y, 0));
        }
        // North East
        if((mesh[y - 1][x] == TILE_QCB) && (mesh[y][x + 1] == TILE_QCB)) {
            ctx->addPip(h->xy_id(x, y, ctx->idf("NE_CHANNEL%d", i)), id_QSBPIP, wire_mesh[y - 1][x]["CHANNEL"][i], wire_mesh[y][x + 1]["CHANNEL"][i], pip_delay, Loc(x, y, 0));
            ctx->addPip(h->xy_id(x, y, ctx->idf("EN_CHANNEL%d", i)), id_QSBPIP, wire_mesh[y][x + 1]["CHANNEL"][i], wire_mesh[y - 1][x]["CHANNEL"][i], pip_delay, Loc(x, y, 0));
        }
        // South West
        if((mesh[y + 1][x] == TILE_QCB) && (mesh[y][x - 1] == TILE_QCB)) {
            ctx->addPip(h->xy_id(x, y, ctx->idf("SW_CHANNEL%d", i)), id_QSBPIP, wire_mesh[y + 1][x]["CHANNEL"][i], wire_mesh[y][x - 1]["CHANNEL"][i], pip_delay, Loc(x, y, 0));
            ctx->addPip(h->xy_id(x, y, ctx->idf("WS_CHANNEL%d", i)), id_QSBPIP, wire_mesh[y][x - 1]["CHANNEL"][i], wire_mesh[y + 1][x]["CHANNEL"][i], pip_delay, Loc(x, y, 0));
        }
    }
}

void Mesh::build_qcb_pips(size_t x, size_t y) {
    assert(mesh[y][x] == TILE_QCB);
    assert(x > 0 && y > 0 && x < DIM_X - 1 && y < DIM_Y - 1);

    const double pip_delay = DUMMY_DELAY;

    // Connect to the CLB above
    if(mesh[y - 1][x] == TILE_CLB) {
        // Inputs
        for(size_t i = 0; i < CLB_INPUTS_PER_SIDE; i++) {
            for(size_t c = 0; c < CHANNEL_WIDTH; c++) {
                // Alternate between even and odd channels for each input
                if(SPARSE_CLB_INPUT && (i % 2 == c % 2))
                    continue;

                auto src = wire_mesh[y][x]["CHANNEL"][c];
                auto dst = wire_mesh[y - 1][x]["SOUTH_IN"][i];
                ctx->addPip(h->xy_id(x, y, ctx->idf("QCB_TO_CLB_SOUTH_IN%d_CHANNEL%d", i, c)), id_QCBPIP, src, dst, QCB_INPUT_DELAY, Loc(x, y, 0));
            }
        }
        // Outputs
        for(size_t i = 0; i < CLB_OUTPUTS_PER_SIDE; i++) {
            for(size_t c = 0; c < CHANNEL_WIDTH; c++) {
                if(SPARSE_CLB_OUTPUT && (i % 2 == c % 2))
                    continue;

                auto src = wire_mesh[y - 1][x]["SOUTH_OUT"][i];
                auto dst = wire_mesh[y][x]["CHANNEL"][c];
                ctx->addPip(h->xy_id(x, y, ctx->idf("CLB_TO_QCB_SOUTH_OUT%d_CHANNEL%d", i, c)), id_QCBPIP, src, dst, pip_delay, Loc(x, y, 0));
            }
        }
    }
    // Connect to the CLB below
    if(mesh[y + 1][x] == TILE_CLB) {
        // Inputs
        for(size_t i = 0; i < CLB_INPUTS_PER_SIDE; i++) {
            for(size_t c = 0; c < CHANNEL_WIDTH; c++) {
                if(SPARSE_CLB_INPUT && (i % 2 != c % 2))
                    continue;

                auto src = wire_mesh[y][x]["CHANNEL"][c];
                auto dst = wire_mesh[y + 1][x]["NORTH_IN"][i];
                ctx->addPip(h->xy_id(x, y, ctx->idf("QCB_TO_CLB_NORTH_IN%d_CHANNEL%d", i, c)), id_QCBPIP, src, dst, QCB_INPUT_DELAY, Loc(x, y, 0));
            }
        }
        // Outputs
        for(size_t i = 0; i < CLB_OUTPUTS_PER_SIDE; i++) {
            for(size_t c = 0; c < CHANNEL_WIDTH; c++) {
                if(SPARSE_CLB_OUTPUT && (i % 2 != c % 2))
                    continue;

                auto src = wire_mesh[y + 1][x]["NORTH_OUT"][i];
                auto dst = wire_mesh[y][x]["CHANNEL"][c];
                ctx->addPip(h->xy_id(x, y, ctx->idf("CLB_TO_QCB_NORTH_OUT%d_CHANNEL%d", i, c)), id_QCBPIP, src, dst, pip_delay, Loc(x, y, 0));
            }
        }
    }
    // Connect to the CLB on the left
    if(mesh[y][x - 1] == TILE_CLB) {
        // Inputs
        for(size_t i = 0; i < CLB_INPUTS_PER_SIDE; i++) {
            for(size_t c = 0; c < CHANNEL_WIDTH; c++) {
                if(SPARSE_CLB_INPUT && (i % 2 != c % 2))
                    continue;

                auto src = wire_mesh[y][x]["CHANNEL"][c];
                auto dst = wire_mesh[y][x - 1]["EAST_IN"][i];
                ctx->addPip(h->xy_id(x, y, ctx->idf("QCB_TO_CLB_EAST_IN%d_CHANNEL%d", i, c)), id_QCBPIP, src, dst, QCB_INPUT_DELAY, Loc(x, y, 0));
            }
        }
        // Outputs
        for(size_t i = 0; i < CLB_OUTPUTS_PER_SIDE; i++) {
            for(size_t c = 0; c < CHANNEL_WIDTH; c++) {
                if(SPARSE_CLB_OUTPUT && (i % 2 != c % 2))
                    continue;

                auto src = wire_mesh[y][x - 1]["EAST_OUT"][i];
                auto dst = wire_mesh[y][x]["CHANNEL"][c];
                ctx->addPip(h->xy_id(x, y, ctx->idf("CLB_TO_QCB_EAST_OUT%d_CHANNEL%d", i, c)), id_QCBPIP, src, dst, pip_delay, Loc(x, y, 0));
            }
        }
    }
    // Connect to the CLB on the right
    if(mesh[y][x + 1] == TILE_CLB) {
        // Inputs
        for(size_t i = 0; i < CLB_INPUTS_PER_SIDE; i++) {
            for(size_t c = 0; c < CHANNEL_WIDTH; c++) {
                if(SPARSE_CLB_INPUT && (i % 2 == c % 2))
                    continue;

                auto src = wire_mesh[y][x]["CHANNEL"][c];
                auto dst = wire_mesh[y][x + 1]["WEST_IN"][i];
                ctx->addPip(h->xy_id(x, y, ctx->idf("QCB_TO_CLB_WEST_IN%d_CHANNEL%d", i, c)), id_QCBPIP, src, dst, QCB_INPUT_DELAY, Loc(x, y, 0));
            }
        }
        // Outputs
        for(size_t i = 0; i < CLB_OUTPUTS_PER_SIDE; i++) {
            for(size_t c = 0; c < CHANNEL_WIDTH; c++) {
                if(SPARSE_CLB_OUTPUT && (i % 2 == c % 2))
                    continue;

                auto src = wire_mesh[y][x + 1]["WEST_OUT"][i];
                auto dst = wire_mesh[y][x]["CHANNEL"][c];
                ctx->addPip(h->xy_id(x, y, ctx->idf("CLB_TO_QCB_WEST_OUT%d_CHANNEL%d", i, c)), id_QCBPIP, src, dst, pip_delay, Loc(x, y, 0));
            }
        }
    }

    // Connect to the IOB above
    if(mesh[y - 1][x] == TILE_IOB) {
        // Inputs
        for(size_t io_in = 0; io_in < IO_PER_IOB * 2; io_in++) {
            for(size_t c = 0; c < CHANNEL_WIDTH; c++) {
                auto src = wire_mesh[y][x]["CHANNEL"][c];
                auto dst = wire_mesh[y - 1][x]["IO_IN"][io_in];
                ctx->addPip(h->xy_id(x, y, ctx->idf("QCB_TO_IOB_SOUTH_IN%d_CHANNEL%d", io_in, c)), id_QCBPIP, src, dst, QCB_INPUT_DELAY, Loc(x, y, 0));
            }
        }
        // Outputs
        for(size_t io_out = 0; io_out < IO_PER_IOB; io_out++) {
            for(size_t c = 0; c < CHANNEL_WIDTH; c++) {
                auto src = wire_mesh[y - 1][x]["IO_OUT"][io_out];
                auto dst = wire_mesh[y][x]["CHANNEL"][c];
                ctx->addPip(h->xy_id(x, y, ctx->idf("IOB_TO_QCB_SOUTH_OUT%d_CHANNEL%d", io_out, c)), id_QCBPIP, src, dst, pip_delay, Loc(x, y, 0));
            }
        }
    }
    // Connect to the IOB below
    if(mesh[y + 1][x] == TILE_IOB) {
        // Inputs
        for(size_t io_in = 0; io_in < IO_PER_IOB * 2; io_in++) {
            for(size_t c = 0; c < CHANNEL_WIDTH; c++) {
                auto src = wire_mesh[y][x]["CHANNEL"][c];
                auto dst = wire_mesh[y + 1][x]["IO_IN"][io_in];
                ctx->addPip(h->xy_id(x, y, ctx->idf("QCB_TO_IOB_NORTH_IN%d_CHANNEL%d", io_in, c)), id_QCBPIP, src, dst, QCB_INPUT_DELAY, Loc(x, y, 0));
            }
        }
        // Outputs
        for(size_t io_out = 0; io_out < IO_PER_IOB; io_out++) {
            for(size_t c = 0; c < CHANNEL_WIDTH; c++) {
                auto src = wire_mesh[y + 1][x]["IO_OUT"][io_out];
                auto dst = wire_mesh[y][x]["CHANNEL"][c];
                ctx->addPip(h->xy_id(x, y, ctx->idf("IOB_TO_QCB_NORTH_OUT%d_CHANNEL%d", io_out, c)), id_QCBPIP, src, dst, pip_delay, Loc(x, y, 0));
            }
        }
    }
    // Connect to the IOB on the left
    if(mesh[y][x - 1] == TILE_IOB) {
        // Inputs
        for(size_t io_in = 0; io_in < IO_PER_IOB * 2; io_in++) {
            for(size_t c = 0; c < CHANNEL_WIDTH; c++) {
                auto src = wire_mesh[y][x]["CHANNEL"][c];
                auto dst = wire_mesh[y][x - 1]["IO_IN"][io_in];
                ctx->addPip(h->xy_id(x, y, ctx->idf("QCB_TO_IOB_EAST_IN%d_CHANNEL%d", io_in, c)), id_QCBPIP, src, dst, QCB_INPUT_DELAY, Loc(x, y, 0));
            }
        }
        // Outputs
        for(size_t io_out = 0; io_out < IO_PER_IOB; io_out++) {
            for(size_t c = 0; c < CHANNEL_WIDTH; c++) {
                auto src = wire_mesh[y][x - 1]["IO_OUT"][io_out];
                auto dst = wire_mesh[y][x]["CHANNEL"][c];
                ctx->addPip(h->xy_id(x, y, ctx->idf("IOB_TO_QCB_EAST_OUT%d_CHANNEL%d", io_out, c)), id_QCBPIP, src, dst, pip_delay, Loc(x, y, 0));
            }
        }
    }
    // Connect to the IOB on the right
    if(mesh[y][x + 1] == TILE_IOB) {
        // Inputs
        for(size_t io_in = 0; io_in < IO_PER_IOB * 2; io_in++) {
            for(size_t c = 0; c < CHANNEL_WIDTH; c++) {
                auto src = wire_mesh[y][x]["CHANNEL"][c];
                auto dst = wire_mesh[y][x + 1]["IO_IN"][io_in];
                ctx->addPip(h->xy_id(x, y, ctx->idf("QCB_TO_IOB_WEST_IN%d_CHANNEL%d", io_in, c)), id_QCBPIP, src, dst, QCB_INPUT_DELAY, Loc(x, y, 0));
            }
        }
        // Outputs
        for(size_t io_out = 0; io_out < IO_PER_IOB; io_out++) {
            for(size_t c = 0; c < CHANNEL_WIDTH; c++) {
                auto src = wire_mesh[y][x + 1]["IO_OUT"][io_out];
                auto dst = wire_mesh[y][x]["CHANNEL"][c];
                ctx->addPip(h->xy_id(x, y, ctx->idf("IOB_TO_QCB_WEST_OUT%d_CHANNEL%d", io_out, c)), id_QCBPIP, src, dst, pip_delay, Loc(x, y, 0));
            }
        }
    }
}

void Mesh::build_clb_pips(size_t x, size_t y) {
    assert(mesh[y][x] == TILE_CLB);

    const double delay = DUMMY_DELAY;

    // Connect slice inputs
    const char* in_dirs[] = {"NORTH_IN", "EAST_IN", "SOUTH_IN", "WEST_IN"};
    for(auto dir : in_dirs) {
        for(size_t slice = 0; slice < SLICES_PER_CLB; slice++) {
            for(size_t lut_in = 0; lut_in < LUT_INPUTS; lut_in++) {
                WireId src = wire_mesh[y][x][dir][lut_in];
                WireId dst = wire_mesh[y][x]["SLICE_IN"][slice * SLICE_INPUTS + lut_in];
                ctx->addPip(h->xy_id(x, y, ctx->idf("SLICE%d_%s_LUT%d", slice, dir, lut_in)), id_CLBPIP, src, dst, delay, Loc(x, y, 0));
            }
            // D input -> Dedicated input
            WireId src = wire_mesh[y][x][dir][LUT_INPUTS];
            WireId dst = wire_mesh[y][x]["SLICE_IN"][slice * SLICE_INPUTS + LUT_INPUTS];
            ctx->addPip(h->xy_id(x, y, ctx->idf("SLICE%d_%s_D", slice, dir)), id_CLBPIP, src, dst, MUX2_DELAY, Loc(x, y, 0));
        }

        // Connect control signals
        WireId src = wire_mesh[y][x][dir][LUT_INPUTS + 1];
        WireId dst = wire_mesh[y][x]["SLICE_CLK"][0];
        ctx->addPip(h->xy_id(x, y, ctx->idf("SLICE_CLK_%s", dir)), id_CLBPIP, src, dst, delay, Loc(x, y, 0));

        dst = wire_mesh[y][x]["SLICE_EN"][0];
        ctx->addPip(h->xy_id(x, y, ctx->idf("SLICE_EN_%s", dir)), id_CLBPIP, src, dst, delay, Loc(x, y, 0));

        dst = wire_mesh[y][x]["SLICE_RST_N"][0];
        ctx->addPip(h->xy_id(x, y, ctx->idf("SLICE_RST_N_%s", dir)), id_CLBPIP, src, dst, delay, Loc(x, y, 0));
    }

    // Connect slice outputs to slice inputs -> Feedback paths
    if(CLB_INTERNAL_FEEDBACK) {
        for(size_t slice_in = 0; slice_in < SLICES_PER_CLB * SLICE_INPUTS; slice_in++) {
            for(size_t slice_out = 0; slice_out < SLICES_PER_CLB * SLICE_OUTPUTS; slice_out++) {
                WireId src = wire_mesh[y][x]["SLICE_OUT"][slice_out];
                WireId dst = wire_mesh[y][x]["SLICE_IN"][slice_in];
                ctx->addPip(h->xy_id(x, y, ctx->idf("SLICE%d_OUT%d_to_SLICE%d_IN%d_FEEDBACK", slice_out / SLICE_OUTPUTS, slice_out % SLICE_OUTPUTS, slice_in / SLICE_INPUTS, slice_in % SLICE_INPUTS)), id_CLBPIP, src, dst, delay, Loc(x, y, 0));
            }
        }

        // DFF Enable and Reset
        for(size_t slice = 0; slice < SLICES_PER_CLB; slice++) {
            for(size_t slice_out = 0; slice_out < SLICE_OUTPUTS; slice_out++) {
                WireId src = wire_mesh[y][x]["SLICE_OUT"][slice * SLICE_OUTPUTS + slice_out];
                WireId dst = wire_mesh[y][x]["SLICE_EN"][0];
                ctx->addPip(h->xy_id(x, y, ctx->idf("SLICE%d_OUT%d_to_SLICE_EN_FEEDBACK", slice, slice_out)), id_CLBPIP, src, dst, delay, Loc(x, y, 0));

                dst = wire_mesh[y][x]["SLICE_RST_N"][0];
                ctx->addPip(h->xy_id(x, y, ctx->idf("SLICE%d_OUT%d_to_SLICE_RST_N_FEEDBACK", slice, slice_out)), id_CLBPIP, src, dst, delay, Loc(x, y, 0));
            }
        }
    }

    // Connect LUT F to FF D
    if(LUT_F_TO_DFF_D) {
        for(size_t slice = 0; slice < SLICES_PER_CLB; slice++) {
            WireId src = wire_mesh[y][x]["SLICE_OUT"][slice * SLICE_OUTPUTS]; // LUT F
            WireId dst = wire_mesh[y][x]["SLICE_IN"][slice * SLICE_INPUTS + LUT_INPUTS]; // FF D
            ctx->addPip(h->xy_id(x, y, ctx->idf("SLICE%d_F_D", slice)), id_CLBPIP, src, dst, MUX2_DELAY, Loc(x, y, 0));
        }
    }

    // Connect slice outputs
    const char* out_dirs[] = {"NORTH_OUT", "EAST_OUT", "SOUTH_OUT", "WEST_OUT"};
    for(auto dir : out_dirs) {
        for(size_t slice = 0; slice < SLICES_PER_CLB; slice++) {
            // F output
            WireId src = wire_mesh[y][x]["SLICE_OUT"][slice * SLICE_OUTPUTS];
            WireId dst = wire_mesh[y][x][dir][0];
            ctx->addPip(h->xy_id(x, y, ctx->idf("SLICE%d_%s_F", slice, dir)), id_CLBPIP, src, dst, delay, Loc(x, y, 0));
            // Q output
            src = wire_mesh[y][x]["SLICE_OUT"][slice * SLICE_OUTPUTS + 1];
            dst = wire_mesh[y][x][dir][1];
            ctx->addPip(h->xy_id(x, y, ctx->idf("SLICE%d_%s_Q", slice, dir)), id_CLBPIP, src, dst, delay, Loc(x, y, 0));
        }
    }
}

void Mesh::build_iob_pips(size_t x, size_t y) {
    assert(mesh[y][x] == TILE_IOB);
}

void Mesh::build_pips() {
    for(size_t y = 0; y < DIM_Y; y++) {
        for(size_t x = 0; x < DIM_X; x++) {
            switch(mesh[y][x]) {
                case TILE_COR:
                    build_corner_pips(x, y);
                    break;
                case TILE_QSB:
                    build_qsb_pips(x, y);
                    break;
                case TILE_QCB:
                    build_qcb_pips(x, y);
                    break;
                case TILE_CLB:
                    build_clb_pips(x, y);
                    break;
                case TILE_IOB:
                    build_iob_pips(x, y);
                    break;
                default:
                    break;
            }
        }
    }
    const size_t count = ctx->getPips().size;
    log_info("%ld Pips built\n", count);


    size_t expected_pips = 0;
    // Check dummy pips
    // Four corners, 2 pips per channel
    expected_pips += 4 * 2 * CHANNEL_WIDTH;

    // Check "real" pips
    // QCB perimiter pips - to 1 other IOB
    expected_pips += (CLBS_X + CLBS_Y) * 2 * CHANNEL_WIDTH * (IO_PER_IOB * 3);
    // QSB perimiter pips - to 3 other QCBs
    expected_pips += (CLBS_X - 1 + CLBS_Y - 1) * 2 * (2 * CHANNEL_WIDTH * 3);
    // QSB core pips - to 4 other QCBs
    expected_pips += ((CLBS_X - 1) * (CLBS_Y - 1)) * (2 * CHANNEL_WIDTH * 6);
    // CLB input pips
    if(SPARSE_CLB_INPUT) expected_pips += CLBS_X * CLBS_Y * CLB_INPUTS_PER_SIDE * CHANNEL_WIDTH / 2 * 4;
    else expected_pips += CLBS_X * CLBS_Y * CLB_INPUTS_PER_SIDE * CHANNEL_WIDTH * 4;
    // CLB output pips
    if(SPARSE_CLB_OUTPUT) expected_pips += CLBS_X * CLBS_Y * CLB_OUTPUTS_PER_SIDE * CHANNEL_WIDTH / 2 * 4;
    else expected_pips += CLBS_X * CLBS_Y * CLB_OUTPUTS_PER_SIDE * CHANNEL_WIDTH * 4;
    // CLB control signals: CLK, EN, RST_N
    expected_pips += CLBS_X * CLBS_Y * 4 * 3;
    // CLB slice internal pips (LUT -> FF)
    if(LUT_F_TO_DFF_D) expected_pips += CLBS_X * CLBS_Y * SLICES_PER_CLB;
    // CLB slice feedback pips
    if(CLB_INTERNAL_FEEDBACK) expected_pips += CLBS_X * CLBS_Y * (SLICES_PER_CLB * SLICE_INPUTS + 2) * (SLICES_PER_CLB * SLICE_OUTPUTS);
    // CLB slice input pips
    expected_pips += CLBS_X * CLBS_Y * SLICES_PER_CLB * SLICE_INPUTS * 4;
    // CLB slice output pips
    expected_pips += CLBS_X * CLBS_Y * SLICES_PER_CLB * SLICE_OUTPUTS * 4;
    log_info("Expected # pips: %ld\n", expected_pips);
    assert(count == expected_pips);
}

void Mesh::build_clb_bels(size_t x, size_t y) {
    assert(mesh[y][x] == TILE_CLB);

    for(size_t slice = 0; slice < SLICES_PER_CLB; slice++) {
        // LUT
        BelId lut = ctx->addBel(h->xy_id(x, y, ctx->idf("SLICE%d_LUT", slice)), id_LUT, Loc(x, y, slice * 2), false, false);
        for(size_t i = 0; i < LUT_INPUTS; i++) {
            ctx->addBelPin(lut, ctx->idf("I[%d]", i), wire_mesh[y][x]["SLICE_IN"][slice * SLICE_INPUTS + i], PortType::PORT_IN);
        }
        ctx->addBelPin(lut, id_F, wire_mesh[y][x]["SLICE_OUT"][slice * SLICE_OUTPUTS], PortType::PORT_OUT);

        // FF
        BelId dff = ctx->addBel(h->xy_id(x, y, ctx->idf("SLICE%d_DFF", slice)), id_DFF, Loc(x, y, slice * 2 + 1), false, false);
        ctx->addBelPin(dff, id_D, wire_mesh[y][x]["SLICE_IN"][slice * SLICE_INPUTS + LUT_INPUTS], PortType::PORT_IN);
        ctx->addBelPin(dff, id_CLK, wire_mesh[y][x]["SLICE_CLK"][0], PortType::PORT_IN);
        ctx->addBelPin(dff, id_EN, wire_mesh[y][x]["SLICE_EN"][0], PortType::PORT_IN);
        ctx->addBelPin(dff, id_RST_N, wire_mesh[y][x]["SLICE_RST_N"][0], PortType::PORT_IN);
        ctx->addBelPin(dff, id_Q, wire_mesh[y][x]["SLICE_OUT"][slice * SLICE_OUTPUTS + 1], PortType::PORT_OUT);
    }
}

void Mesh::build_iob_bels(size_t x, size_t y) {
    assert(mesh[y][x] == TILE_IOB);

    for(size_t io = 0; io < IO_PER_IOB; io++) {
        BelId bel = ctx->addBel(h->xy_id(x, y, ctx->idf("IO%d", io)), id_IOB, Loc(x, y, io), false, false);
        ctx->addBelPin(bel, id_I, wire_mesh[y][x]["IO_IN"][io * 2], PortType::PORT_IN);
        ctx->addBelPin(bel, id_EN, wire_mesh[y][x]["IO_IN"][io * 2 + 1], PortType::PORT_IN);
        ctx->addBelPin(bel, id_PAD, wire_mesh[y][x]["IO_INOUT"][io], PortType::PORT_INOUT);
        ctx->addBelPin(bel, id_O, wire_mesh[y][x]["IO_OUT"][io], PortType::PORT_OUT);
    }
}

void Mesh::build_bels() {
    for(size_t y = 0; y < DIM_Y; y++) {
        for(size_t x = 0; x < DIM_X; x++) {
            switch(mesh[y][x]) {
                case TILE_CLB:
                    build_clb_bels(x, y);
                    break;
                case TILE_IOB:
                    build_iob_bels(x, y);
                    break;
                default:
                    break;
            }
        }
    }

    const size_t count = ctx->getBels().size;
    log_info("%ld BELs built\n", count);

    size_t expected_bels = 0;
    // CLB Slices -> LUT and FF
    expected_bels += CLBS_X * CLBS_Y * SLICES_PER_CLB * 2;
    // IOB
    expected_bels += (CLBS_X + CLBS_Y) * 2 * IO_PER_IOB;
    log_info("Expected # BELs: %ld\n", expected_bels);
    assert(count == expected_bels);
}

void Mesh::update_lut_timing(const CellInfo *ci) {
    for(size_t i = 0; i < LUT_INPUTS; i++) {
        ctx->addCellTimingDelay(ci->name, ctx->idf("I[%d]", i), id_F, LUT_DELAY);
    }
}

void Mesh::update_dff_timing(const CellInfo *ci) {
    ctx->addCellTimingClock(ci->name, id_CLK);
    ctx->addCellTimingSetupHold(ci->name, id_D, id_CLK, DFF_SETUP, DFF_HOLD);
    ctx->addCellTimingClockToOut(ci->name, id_Q, id_CLK, DFF_CLK_TO_Q);
}

void Mesh::update_timing() {
    for(auto &cell : ctx->cells) {
        CellInfo *ci = cell.second.get();
        if(ci->type == id_LUT)      update_lut_timing(ci);
        else if(ci->type == id_DFF) update_dff_timing(ci);
        else if(ci->type == id_IOB)  { }
        else if(ci->type == id_IBUF) { }
        else if(ci->type == id_OBUF) { }
        else log_error("Unknown cell type %s\n", ci->type.c_str(ctx));
    }
}

NEXTPNR_NAMESPACE_END
