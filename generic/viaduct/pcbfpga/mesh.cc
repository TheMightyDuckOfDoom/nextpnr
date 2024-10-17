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
bool is_perimeter(size_t x, size_t y, size_t dimX, size_t dimY) {
    return x == 0 || x == dimX - 1 || y == 0 || y == dimY - 1;
}

bool is_corner(size_t x, size_t y, size_t dimX, size_t dimY) {
    return (x == 0 && y == 0) || (x == dimX - 1 && y == 0) || (x == 0 && y == dimY - 1) || (x == dimX - 1 && y == dimY - 1);
}

bool is_secondary_corner(size_t x, size_t y, size_t dimX, size_t dimY) {
    return (x == 1 && y == 1) || (x == dimX - 2 && y == 1) || (x == 1 && y == dimY - 2) || (x == dimX - 2 && y == dimY - 2);
}

bool is_io(size_t x, size_t y, size_t dimX, size_t dimY) {
    return is_perimeter(x, y, dimX, dimY) && !is_corner(x, y, dimX, dimY) && ((x + y) % 2 == 0);
}

bool is_clb(size_t x, size_t y, size_t dimX, size_t dimY) {
    return !is_perimeter(x, y, dimX, dimY) && (x % 2 == 0) && (y % 2 == 0);
}

bool is_qsb(size_t x, size_t y, size_t dimX, size_t dimY) {
    return !is_perimeter(x, y, dimX, dimY) && (x % 2 == 1) && (y % 2 == 1);
}

Mesh::Mesh(Context *ctx, ViaductHelpers *h, size_t clbs_x, size_t clbs_y, size_t channel_width) {
    this->ctx = ctx;
    this->h = h;
    this->clbs_x = clbs_x;
    this->clbs_y = clbs_y;
    this->channel_width = channel_width;
    this->dimX = clbs_x * 2 + 3;
    this->dimY = clbs_y * 2 + 3;
}

void Mesh::build() {
    build_mesh();
    print();
    build_wires();
    build_pips();
    build_bels();
}

void Mesh::print() {
    log_info("    ");
    for(size_t x = 0; x < dimX; x++) {
        printf("%3ld ", x);
    }
    printf("\n");
    
    for(size_t y = 0; y < dimY; y++) {
        log_info("%3ld ", y);
        for(size_t x = 0; x < mesh[y].size(); x++) {
            printf("%s ", tile_type_to_string(mesh[y][x]));
        }
        printf("\n");
    }
}

void Mesh::build_mesh() {
    mesh.resize(dimY, std::vector<tile_type_t>(dimX, TILE_NONE));

    size_t count[6] = {0, 0, 0, 0, 0, 0};

    for(size_t y = 0; y < dimY; y++) {
        for(size_t x = 0; x < dimX; x++) {
            if(is_io(x, y, dimX, dimY))
                mesh[y][x] = TILE_IOB;
            else if(is_clb(x, y, dimX, dimY))
                mesh[y][x] = TILE_CLB;
            else if(is_secondary_corner(x, y, dimX, dimY))
                mesh[y][x] = TILE_COR;
            else if(is_qsb(x, y, dimX, dimY))
                mesh[y][x] = TILE_QSB;
            else if(!is_perimeter(x, y, dimX, dimY))
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

    std::vector<WireId> wires(channel_width);
    for(size_t i = 0; i < channel_width; i++) {
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
        std::vector<WireId> wires(clb_inputs_per_side);
        for(size_t i = 0; i < clb_inputs_per_side; i++) {
            wires[i] = ctx->addWire(h->xy_id(x, y, ctx->idf("%s%d", dir, i)), ctx->id(dir), x, y);
        }
        wire_map[dir] = wires;
    }
    
    // Output wires
    const char* out_dirs[] = {"NORTH_OUT", "EAST_OUT", "SOUTH_OUT", "WEST_OUT"};
    for(auto dir : out_dirs) {
        std::vector<WireId> wires(clb_outputs_per_side);
        for(size_t i = 0; i < clb_outputs_per_side; i++) {
            wires[i] = ctx->addWire(h->xy_id(x, y, ctx->idf("%s%d", dir, i)), ctx->id(dir), x, y);
        }
        wire_map[dir] = wires;
    }

    /// Slice wires
    wire_map["SLICE_CLK"] = std::vector<WireId>(1, ctx->addWire(h->xy_id(x, y, ctx->idf("SLICE_CLK")), ctx->id("SLICE_CLK"), x, y));

    // Inputs
    wire_map["SLICE_IN"] = std::vector<WireId>(slices_per_clb * slice_inputs);
    for(size_t i = 0; i < slices_per_clb; i++) {
        for(size_t j = 0; j < lut_inputs; j++) {
            wire_map["SLICE_IN"][i * slice_inputs + j] = ctx->addWire(h->xy_id(x, y, ctx->idf("SLICE%d_LUT%d", i, j)), ctx->id("SLICE_LUT"), x, y);
        }
        wire_map["SLICE_IN"][i * slice_inputs + lut_inputs] = ctx->addWire(h->xy_id(x, y, ctx->idf("SLICE%d_D", i)), ctx->id("SLICE_D"), x, y);
    }

    // Outputs
    wire_map["SLICE_OUT"] = std::vector<WireId>(slices_per_clb * slice_outputs);
    for(size_t i = 0; i < slices_per_clb; i++) {
        wire_map["SLICE_OUT"][i * slice_outputs] = ctx->addWire(h->xy_id(x, y, ctx->idf("SLICE%d_F", i)), ctx->id("SLICE_F"), x, y);
        wire_map["SLICE_OUT"][i * slice_outputs + 1] = ctx->addWire(h->xy_id(x, y, ctx->idf("SLICE%d_Q", i)), ctx->id("SLICE_Q"), x, y);
    }

    return wire_map;
}

wire_map_t Mesh::build_iob_wires(size_t x, size_t y) {
    assert(mesh[y][x] == TILE_IOB);
    wire_map_t wire_map;

    // IO wires
    wire_map["IO_IN"] = std::vector<WireId>(io_per_iob * 2);
    wire_map["IO_INOUT"] = std::vector<WireId>(io_per_iob);
    wire_map["IO_OUT"] = std::vector<WireId>(io_per_iob);
    for(size_t i = 0; i < io_per_iob; i++) {
        wire_map["IO_IN"][i * 2] = ctx->addWire(h->xy_id(x, y, ctx->idf("IO%d_I", i)), id_I, x, y);
        wire_map["IO_IN"][i * 2 + 1] = ctx->addWire(h->xy_id(x, y, ctx->idf("IO%d_EN", i)), id_EN, x, y);
        wire_map["IO_INOUT"][i] = ctx->addWire(h->xy_id(x, y, ctx->idf("IO%d_PAD", i)), id_PAD, x, y);
        wire_map["IO_OUT"][i] = ctx->addWire(h->xy_id(x, y, ctx->idf("IO%d_O", i)), id_O, x, y);
    }

    return wire_map;
}

void Mesh::build_wires() {
    wire_mesh.resize(dimY, std::vector<wire_map_t>(dimX));

    for(size_t y = 0; y < dimY; y++) {
        for(size_t x = 0; x < dimX; x++) {
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

size_t Mesh::build_corner_pips(size_t x, size_t y) {
    assert(is_secondary_corner(x, y, dimX, dimY));

    const double dummy_delay = 0.05;

    // Top left corner
    if(x == 1 && y == 1) {
        // Connect bottom qcb to right qcb
        for(size_t i = 0; i < channel_width; i++) {
            WireId bottom = wire_mesh[y + 1][x]["CHANNEL"][i];
            WireId right = wire_mesh[y][x + 1]["CHANNEL"][i];
            ctx->addPip(h->xy_id(x, y, ctx->idf("DUMMY1_CHANNEL%d", i)), id_CORNERPIP, right, bottom, dummy_delay, Loc(x, y, 0));
            ctx->addPip(h->xy_id(x, y, ctx->idf("DUMMY2_CHANNEL%d", i)), id_CORNERPIP, bottom, right, dummy_delay, Loc(x, y, 0));
        }

        return 2 * channel_width;
    }

    // Top right corner
    if(x == dimX-2 && y == 1) {
        // Connect bottom qcb to left qcb
        for(size_t i = 0; i < channel_width; i++) {
            WireId bottom = wire_mesh[y + 1][x]["CHANNEL"][i];
            WireId left = wire_mesh[y][x - 1]["CHANNEL"][i];
            ctx->addPip(h->xy_id(x, y, ctx->idf("DUMMY1_CHANNEL%d", i)), id_CORNERPIP, left, bottom, dummy_delay, Loc(x, y, 0));
            ctx->addPip(h->xy_id(x, y, ctx->idf("DUMMY2_CHANNEL%d", i)), id_CORNERPIP, bottom, left, dummy_delay, Loc(x, y, 0));
        }

        return 2 * channel_width;
    }

    // Bottom left corner
    if(x == 1 && y == dimY-2) {
        // Connect top qcb to right qcb
        for(size_t i = 0; i < channel_width; i++) {
            WireId top = wire_mesh[y - 1][x]["CHANNEL"][i];
            WireId right = wire_mesh[y][x + 1]["CHANNEL"][i];
            ctx->addPip(h->xy_id(x, y, ctx->idf("DUMMY1_CHANNEL%d", i)), id_CORNERPIP, right, top, dummy_delay, Loc(x, y, 0));
            ctx->addPip(h->xy_id(x, y, ctx->idf("DUMMY2_CHANNEL%d", i)), id_CORNERPIP, top, right, dummy_delay, Loc(x, y, 0));
        }

        return 2 * channel_width;
    }

    // Bottom right corner
    if(x == (dimX-2) && y == (dimY-2)) {
        // Connect top qcb to left qcb
        for(size_t i = 0; i < channel_width; i++) {
            WireId top = wire_mesh[y - 1][x]["CHANNEL"][i];
            WireId left = wire_mesh[y][x - 1]["CHANNEL"][i];
            ctx->addPip(h->xy_id(x, y, ctx->idf("DUMMY1_CHANNEL%d", i)), id_CORNERPIP, left, top, dummy_delay, Loc(x, y, 0));
            ctx->addPip(h->xy_id(x, y, ctx->idf("DUMMY2_CHANNEL%d", i)), id_CORNERPIP, top, left, dummy_delay, Loc(x, y, 0));
        }

        return 2 * channel_width;
    }

    assert(false);
    return 0;
}

size_t Mesh::build_qsb_pips(size_t x, size_t y) {
    // Make sure this tile is a QSB
    assert(is_qsb(x, y, dimX, dimY));

    const double pip_delay = 0.05;

    size_t count = 0;
    for(size_t i = 0; i < channel_width; i++) {
        // Noth south
        if((mesh[y - 1][x] == TILE_QCB) && (mesh[y + 1][x] == TILE_QCB)) {
            ctx->addPip(h->xy_id(x, y, ctx->idf("NS_CHANNEL%d", i)), id_QSBPIP, wire_mesh[y - 1][x]["CHANNEL"][i], wire_mesh[y + 1][x]["CHANNEL"][i], pip_delay, Loc(x, y, 0));
            ctx->addPip(h->xy_id(x, y, ctx->idf("SN_CHANNEL%d", i)), id_QSBPIP, wire_mesh[y + 1][x]["CHANNEL"][i], wire_mesh[y - 1][x]["CHANNEL"][i], pip_delay, Loc(x, y, 0));
            count += 2; 
        }
        // East West
        if((mesh[y][x - 1] == TILE_QCB) && (mesh[y][x + 1] == TILE_QCB)) {
            ctx->addPip(h->xy_id(x, y, ctx->idf("EW_CHANNEL%d", i)), id_QSBPIP, wire_mesh[y][x - 1]["CHANNEL"][i], wire_mesh[y][x + 1]["CHANNEL"][i], pip_delay, Loc(x, y, 0));
            ctx->addPip(h->xy_id(x, y, ctx->idf("WE_CHANNEL%d", i)), id_QSBPIP, wire_mesh[y][x + 1]["CHANNEL"][i], wire_mesh[y][x - 1]["CHANNEL"][i], pip_delay, Loc(x, y, 0));
            count += 2; 
        }
        // North West
        if((mesh[y - 1][x] == TILE_QCB) && (mesh[y][x - 1] == TILE_QCB)) {
            ctx->addPip(h->xy_id(x, y, ctx->idf("NW_CHANNEL%d", i)), id_QSBPIP, wire_mesh[y - 1][x]["CHANNEL"][i], wire_mesh[y][x - 1]["CHANNEL"][i], pip_delay, Loc(x, y, 0));
            ctx->addPip(h->xy_id(x, y, ctx->idf("WN_CHANNEL%d", i)), id_QSBPIP, wire_mesh[y][x - 1]["CHANNEL"][i], wire_mesh[y - 1][x]["CHANNEL"][i], pip_delay, Loc(x, y, 0));
            count += 2; 
        }
        // South East
        if((mesh[y + 1][x] == TILE_QCB) && (mesh[y][x + 1] == TILE_QCB)) {
            ctx->addPip(h->xy_id(x, y, ctx->idf("SE_CHANNEL%d", i)), id_QSBPIP, wire_mesh[y + 1][x]["CHANNEL"][i], wire_mesh[y][x + 1]["CHANNEL"][i], pip_delay, Loc(x, y, 0));
            ctx->addPip(h->xy_id(x, y, ctx->idf("ES_CHANNEL%d", i)), id_QSBPIP, wire_mesh[y][x + 1]["CHANNEL"][i], wire_mesh[y + 1][x]["CHANNEL"][i], pip_delay, Loc(x, y, 0));
            count += 2; 
        }
        // North East
        if((mesh[y - 1][x] == TILE_QCB) && (mesh[y][x + 1] == TILE_QCB)) {
            ctx->addPip(h->xy_id(x, y, ctx->idf("NE_CHANNEL%d", i)), id_QSBPIP, wire_mesh[y - 1][x]["CHANNEL"][i], wire_mesh[y][x + 1]["CHANNEL"][i], pip_delay, Loc(x, y, 0));
            ctx->addPip(h->xy_id(x, y, ctx->idf("EN_CHANNEL%d", i)), id_QSBPIP, wire_mesh[y][x + 1]["CHANNEL"][i], wire_mesh[y - 1][x]["CHANNEL"][i], pip_delay, Loc(x, y, 0));
            count += 2; 
        }
        // South West
        if((mesh[y + 1][x] == TILE_QCB) && (mesh[y][x - 1] == TILE_QCB)) {
            ctx->addPip(h->xy_id(x, y, ctx->idf("SW_CHANNEL%d", i)), id_QSBPIP, wire_mesh[y + 1][x]["CHANNEL"][i], wire_mesh[y][x - 1]["CHANNEL"][i], pip_delay, Loc(x, y, 0));
            ctx->addPip(h->xy_id(x, y, ctx->idf("WS_CHANNEL%d", i)), id_QSBPIP, wire_mesh[y][x - 1]["CHANNEL"][i], wire_mesh[y + 1][x]["CHANNEL"][i], pip_delay, Loc(x, y, 0));
            count += 2; 
        }
    }

    return count;
}

size_t Mesh::build_qcb_pips(size_t x, size_t y) {
    assert(mesh[y][x] == TILE_QCB);
    assert(x > 0 && y > 0 && x < dimX - 1 && y < dimY - 1);

    const double pip_delay = 0.05;
    size_t count = 0;

    // Connect to the CLB above
    if(mesh[y - 1][x] == TILE_CLB) {
        // Inputs
        for(size_t i = 0; i < clb_inputs_per_side; i++) {
            for(size_t c = 0; c < channel_width; c++) {
                auto src = wire_mesh[y][x]["CHANNEL"][c];
                auto dst = wire_mesh[y - 1][x]["SOUTH_IN"][i];
                ctx->addPip(h->xy_id(x, y, ctx->idf("QCB_TO_CLB_SOUTH_IN%d_CHANNEL%d", i, c)), id_QCBPIP, src, dst, pip_delay, Loc(x, y, 0));
                count++;
            }
        }
        // Outputs
        for(size_t i = 0; i < clb_outputs_per_side; i++) {
            for(size_t c = 0; c < channel_width; c++) {
                auto src = wire_mesh[y - 1][x]["SOUTH_OUT"][i];
                auto dst = wire_mesh[y][x]["CHANNEL"][c];
                ctx->addPip(h->xy_id(x, y, ctx->idf("CLB_TO_QCB_SOUTH_OUT%d_CHANNEL%d", i, c)), id_QCBPIP, src, dst, pip_delay, Loc(x, y, 0));
                count++;
            }
        }
    }
    // Connect to the CLB below
    if(mesh[y + 1][x] == TILE_CLB) {
        // Inputs
        for(size_t i = 0; i < clb_inputs_per_side; i++) {
            for(size_t c = 0; c < channel_width; c++) {
                auto src = wire_mesh[y][x]["CHANNEL"][c];
                auto dst = wire_mesh[y + 1][x]["NORTH_IN"][i];
                ctx->addPip(h->xy_id(x, y, ctx->idf("QCB_TO_CLB_NORTH_IN%d_CHANNEL%d", i, c)), id_QCBPIP, src, dst, pip_delay, Loc(x, y, 0));
                count++;
            }
        }
        // Outputs
        for(size_t i = 0; i < clb_outputs_per_side; i++) {
            for(size_t c = 0; c < channel_width; c++) {
                auto src = wire_mesh[y + 1][x]["NORTH_OUT"][i];
                auto dst = wire_mesh[y][x]["CHANNEL"][c];
                ctx->addPip(h->xy_id(x, y, ctx->idf("CLB_TO_QCB_NORTH_OUT%d_CHANNEL%d", i, c)), id_QCBPIP, src, dst, pip_delay, Loc(x, y, 0));
                count++;
            }
        }
    }
    // Connect to the CLB on the left
    if(mesh[y][x - 1] == TILE_CLB) {
        // Inputs
        for(size_t i = 0; i < clb_inputs_per_side; i++) {
            for(size_t c = 0; c < channel_width; c++) {
                auto src = wire_mesh[y][x]["CHANNEL"][c];
                auto dst = wire_mesh[y][x - 1]["EAST_IN"][i];
                ctx->addPip(h->xy_id(x, y, ctx->idf("QCB_TO_CLB_EAST_IN%d_CHANNEL%d", i, c)), id_QCBPIP, src, dst, pip_delay, Loc(x, y, 0));
                count++;
            }
        }
        // Outputs
        for(size_t i = 0; i < clb_outputs_per_side; i++) {
            for(size_t c = 0; c < channel_width; c++) {
                auto src = wire_mesh[y][x - 1]["EAST_OUT"][i];
                auto dst = wire_mesh[y][x]["CHANNEL"][c];
                ctx->addPip(h->xy_id(x, y, ctx->idf("CLB_TO_QCB_EAST_OUT%d_CHANNEL%d", i, c)), id_QCBPIP, src, dst, pip_delay, Loc(x, y, 0));
                count++;
            }
        }
    }
    // Connect to the CLB on the right
    if(mesh[y][x + 1] == TILE_CLB) {
        // Inputs
        for(size_t i = 0; i < clb_inputs_per_side; i++) {
            for(size_t c = 0; c < channel_width; c++) {
                auto src = wire_mesh[y][x]["CHANNEL"][c];
                auto dst = wire_mesh[y][x + 1]["WEST_IN"][i];
                ctx->addPip(h->xy_id(x, y, ctx->idf("QCB_TO_CLB_WEST_IN%d_CHANNEL%d", i, c)), id_QCBPIP, src, dst, pip_delay, Loc(x, y, 0));
                count++;
            }
        }
        // Outputs
        for(size_t i = 0; i < clb_outputs_per_side; i++) {
            for(size_t c = 0; c < channel_width; c++) {
                auto src = wire_mesh[y][x + 1]["WEST_OUT"][i];
                auto dst = wire_mesh[y][x]["CHANNEL"][c];
                ctx->addPip(h->xy_id(x, y, ctx->idf("CLB_TO_QCB_WEST_OUT%d_CHANNEL%d", i, c)), id_QCBPIP, src, dst, pip_delay, Loc(x, y, 0));
                count++;
            }
        }
    }

    // Connect to the IOB above
    if(mesh[y - 1][x] == TILE_IOB) {
        // Inputs
        for(size_t io_in = 0; io_in < io_per_iob * 2; io_in++) {
            for(size_t c = 0; c < channel_width; c++) {
                auto src = wire_mesh[y][x]["CHANNEL"][c];
                auto dst = wire_mesh[y - 1][x]["IO_IN"][io_in];
                ctx->addPip(h->xy_id(x, y, ctx->idf("QCB_TO_IOB_SOUTH_IN%d_CHANNEL%d", io_in, c)), id_QCBPIP, src, dst, pip_delay, Loc(x, y, 0));
                count++;
            }
        }
        // Outputs
        for(size_t io_out = 0; io_out < io_per_iob; io_out++) {
            for(size_t c = 0; c < channel_width; c++) {
                auto src = wire_mesh[y - 1][x]["IO_OUT"][io_out];
                auto dst = wire_mesh[y][x]["CHANNEL"][c];
                ctx->addPip(h->xy_id(x, y, ctx->idf("IOB_TO_QCB_SOUTH_OUT%d_CHANNEL%d", io_out, c)), id_QCBPIP, src, dst, pip_delay, Loc(x, y, 0));
                count++;
            }
        }
    }
    // Connect to the IOB below
    if(mesh[y + 1][x] == TILE_IOB) {
        // Inputs
        for(size_t io_in = 0; io_in < io_per_iob * 2; io_in++) {
            for(size_t c = 0; c < channel_width; c++) {
                auto src = wire_mesh[y][x]["CHANNEL"][c];
                auto dst = wire_mesh[y + 1][x]["IO_IN"][io_in];
                ctx->addPip(h->xy_id(x, y, ctx->idf("QCB_TO_IOB_NORTH_IN%d_CHANNEL%d", io_in, c)), id_QCBPIP, src, dst, pip_delay, Loc(x, y, 0));
                count++;
            }
        }
        // Outputs
        for(size_t io_out = 0; io_out < io_per_iob; io_out++) {
            for(size_t c = 0; c < channel_width; c++) {
                auto src = wire_mesh[y + 1][x]["IO_OUT"][io_out];
                auto dst = wire_mesh[y][x]["CHANNEL"][c];
                ctx->addPip(h->xy_id(x, y, ctx->idf("IOB_TO_QCB_NORTH_OUT%d_CHANNEL%d", io_out, c)), id_QCBPIP, src, dst, pip_delay, Loc(x, y, 0));
                count++;
            }
        }
    }
    // Connect to the IOB on the left
    if(mesh[y][x - 1] == TILE_IOB) {
        // Inputs
        for(size_t io_in = 0; io_in < io_per_iob * 2; io_in++) {
            for(size_t c = 0; c < channel_width; c++) {
                auto src = wire_mesh[y][x]["CHANNEL"][c];
                auto dst = wire_mesh[y][x - 1]["IO_IN"][io_in];
                ctx->addPip(h->xy_id(x, y, ctx->idf("QCB_TO_IOB_EAST_IN%d_CHANNEL%d", io_in, c)), id_QCBPIP, src, dst, pip_delay, Loc(x, y, 0));
                count++;
            }
        }
        // Outputs
        for(size_t io_out = 0; io_out < io_per_iob; io_out++) {
            for(size_t c = 0; c < channel_width; c++) {
                auto src = wire_mesh[y][x - 1]["IO_OUT"][io_out];
                auto dst = wire_mesh[y][x]["CHANNEL"][c];
                ctx->addPip(h->xy_id(x, y, ctx->idf("IOB_TO_QCB_EAST_OUT%d_CHANNEL%d", io_out, c)), id_QCBPIP, src, dst, pip_delay, Loc(x, y, 0));
                count++;
            }
        }
    }
    // Connect to the IOB on the right
    if(mesh[y][x + 1] == TILE_IOB) {
        // Inputs
        for(size_t io_in = 0; io_in < io_per_iob * 2; io_in++) {
            for(size_t c = 0; c < channel_width; c++) {
                auto src = wire_mesh[y][x]["CHANNEL"][c];
                auto dst = wire_mesh[y][x + 1]["IO_IN"][io_in];
                ctx->addPip(h->xy_id(x, y, ctx->idf("QCB_TO_IOB_WEST_IN%d_CHANNEL%d", io_in, c)), id_QCBPIP, src, dst, pip_delay, Loc(x, y, 0));
                count++;
            }
        }
        // Outputs
        for(size_t io_out = 0; io_out < io_per_iob; io_out++) {
            for(size_t c = 0; c < channel_width; c++) {
                auto src = wire_mesh[y][x + 1]["IO_OUT"][io_out];
                auto dst = wire_mesh[y][x]["CHANNEL"][c];
                ctx->addPip(h->xy_id(x, y, ctx->idf("IOB_TO_QCB_WEST_OUT%d_CHANNEL%d", io_out, c)), id_QCBPIP, src, dst, pip_delay, Loc(x, y, 0));
                count++;
            }
        }
    }

    return count;
}

size_t Mesh::build_clb_pips(size_t x, size_t y) {
    assert(mesh[y][x] == TILE_CLB);
    size_t count = 0;

    const double delay = 0.05;

    // Connect slice inputs
    const char* in_dirs[] = {"NORTH_IN", "EAST_IN", "SOUTH_IN", "WEST_IN"};
    for(auto dir : in_dirs) {
        for(size_t slice = 0; slice < slices_per_clb; slice++) {
            for(size_t lut_in = 0; lut_in < lut_inputs; lut_in++) {
                WireId src = wire_mesh[y][x][dir][lut_in];
                WireId dst = wire_mesh[y][x]["SLICE_IN"][slice * slice_inputs + lut_in];
                ctx->addPip(h->xy_id(x, y, ctx->idf("SLICE%d_%s_LUT%d", slice, dir, lut_in)), id_CLBPIP, src, dst, delay, Loc(x, y, 0));
                count++;
            }
            // D input
            WireId src = wire_mesh[y][x][dir][lut_inputs];
            WireId dst = wire_mesh[y][x]["SLICE_IN"][slice * slice_inputs + lut_inputs];
            ctx->addPip(h->xy_id(x, y, ctx->idf("SLICE%d_%s_D", slice, dir)), id_CLBPIP, src, dst, delay, Loc(x, y, 0));
            count++;
        }

        // Connect control signals
        WireId src = wire_mesh[y][x][dir][lut_inputs + 1];
        WireId dst = wire_mesh[y][x]["SLICE_CLK"][0];
        ctx->addPip(h->xy_id(x, y, ctx->idf("SLICE_CLK_%s", dir)), id_CLBPIP, src, dst, delay, Loc(x, y, 0));
        count++;
    }

    // Connect slice outputs
    const char* out_dirs[] = {"NORTH_OUT", "EAST_OUT", "SOUTH_OUT", "WEST_OUT"};
    for(auto dir : out_dirs) {
        for(size_t slice = 0; slice < slices_per_clb; slice++) {
            // F output
            WireId src = wire_mesh[y][x]["SLICE_OUT"][slice * slice_outputs];
            WireId dst = wire_mesh[y][x][dir][0];
            ctx->addPip(h->xy_id(x, y, ctx->idf("SLICE%d_%s_F", slice, dir)), id_CLBPIP, src, dst, delay, Loc(x, y, 0));
            count++;
            // Q output
            src = wire_mesh[y][x]["SLICE_OUT"][slice * slice_outputs + 1];
            dst = wire_mesh[y][x][dir][1];
            ctx->addPip(h->xy_id(x, y, ctx->idf("SLICE%d_%s_Q", slice, dir)), id_CLBPIP, src, dst, delay, Loc(x, y, 0));
            count++;
        }
    }

    return count;
}

size_t Mesh::build_iob_pips(size_t x, size_t y) {
    assert(mesh[y][x] == TILE_IOB);
    return 0;
}

void Mesh::build_pips() {
    size_t count = 0;
    size_t dummy_pips = 0;

    for(size_t y = 0; y < dimY; y++) {
        for(size_t x = 0; x < dimX; x++) {
            switch(mesh[y][x]) {
                case TILE_COR:
                    dummy_pips += build_corner_pips(x, y);
                    break;
                case TILE_QSB:
                    count += build_qsb_pips(x, y);
                    break;
                case TILE_QCB:
                    count += build_qcb_pips(x, y);
                    break;
                case TILE_CLB:
                    count += build_clb_pips(x, y);
                    break;
                case TILE_IOB:
                    count += build_iob_pips(x, y);
                    break;
                default:
                    break;
            }
        }
    }
    log_info("%ld Pips, %ld Dummy-Pips built\n", count, dummy_pips);

    // Check dummy pips
    // Four corners, 2 pips per channel
    assert(dummy_pips == 4 * 2 * channel_width);

    // Check "real" pips
    size_t expected_pips = 0;
    // QCB perimiter pips - to 1 other IOB
    expected_pips += (clbs_x + clbs_y) * 2 * channel_width * (io_per_iob * 3);
    // QSB perimiter pips - to 3 other QCBs
    expected_pips += (clbs_x - 1 + clbs_y - 1) * 2 * (2 * channel_width * 3);
    // QSB core pips - to 4 other QCBs
    expected_pips += ((clbs_x - 1) * (clbs_y - 1)) * (2 * channel_width * 6);
    // CLB input and output pips
    expected_pips += clbs_x * clbs_y * (clb_inputs_per_side + clb_outputs_per_side) * channel_width * 4;
    // CLB control signals
    expected_pips += clbs_x * clbs_y * 4;
    // CLB slice input pips
    expected_pips += clbs_x * clbs_y * slices_per_clb * slice_inputs * 4;
    // CLB slice output pips
    expected_pips += clbs_x * clbs_y * slices_per_clb * slice_outputs * 4;
    log_info("Expected # pips: %ld\n", expected_pips);
    assert(count == expected_pips);
}

size_t Mesh::build_clb_bels(size_t x, size_t y) {
    assert(mesh[y][x] == TILE_CLB);
    size_t count = 0;

    for(size_t slice = 0; slice < slices_per_clb; slice++) {
        // LUT
        BelId lut = ctx->addBel(h->xy_id(x, y, ctx->idf("SLICE%d_LUT", slice)), id_LUT, Loc(x, y, slice * 2), false, false);
        for(size_t i = 0; i < lut_inputs; i++) {
            ctx->addBelPin(lut, ctx->idf("I[%d]", i), wire_mesh[y][x]["SLICE_IN"][slice * slice_inputs + i], PortType::PORT_IN);
        }
        ctx->addBelPin(lut, id_F, wire_mesh[y][x]["SLICE_OUT"][slice * slice_outputs], PortType::PORT_OUT);
        count++;

        // FF
        BelId dff = ctx->addBel(h->xy_id(x, y, ctx->idf("SLICE%d_DFF", slice)), id_DFF, Loc(x, y, slice * 2 + 1), false, false);
        ctx->addBelPin(dff, id_D, wire_mesh[y][x]["SLICE_IN"][slice * slice_inputs + lut_inputs], PortType::PORT_IN);
        ctx->addBelPin(dff, id_CLK, wire_mesh[y][x]["SLICE_CLK"][0], PortType::PORT_IN);
        ctx->addBelPin(dff, id_Q, wire_mesh[y][x]["SLICE_OUT"][slice * slice_outputs + 1], PortType::PORT_OUT);
        count++;
    }

    return count;
}

size_t Mesh::build_iob_bels(size_t x, size_t y) {
    assert(mesh[y][x] == TILE_IOB);
    size_t count = 0;

    for(size_t io = 0; io < io_per_iob; io++) {
        BelId bel = ctx->addBel(h->xy_id(x, y, ctx->idf("IO%d", io)), id_IOB, Loc(x, y, io), false, false);
        ctx->addBelPin(bel, id_I, wire_mesh[y][x]["IO_IN"][io * 2], PortType::PORT_IN);
        ctx->addBelPin(bel, id_EN, wire_mesh[y][x]["IO_IN"][io * 2 + 1], PortType::PORT_IN);
        ctx->addBelPin(bel, id_PAD, wire_mesh[y][x]["IO_INOUT"][io], PortType::PORT_INOUT);
        ctx->addBelPin(bel, id_O, wire_mesh[y][x]["IO_OUT"][io], PortType::PORT_OUT);
        count++;
    }

    return count;
}

void Mesh::build_bels() {
    size_t count = 0;

    for(size_t y = 0; y < dimY; y++) {
        for(size_t x = 0; x < dimX; x++) {
            switch(mesh[y][x]) {
                case TILE_CLB:
                    count += build_clb_bels(x, y);
                    break;
                case TILE_IOB:
                    count += build_iob_bels(x, y);
                    break;
                default:
                    break;
            }
        }
    }

    log_info("%ld BELs built\n", count);

    size_t expected_bels = 0;
    // CLB Slices -> LUT and FF
    expected_bels += clbs_x * clbs_y * slices_per_clb * 2;
    // IOB
    expected_bels += (clbs_x + clbs_y) * 2 * io_per_iob;
    log_info("Expected # BELs: %ld\n", expected_bels);
    assert(count == expected_bels);
}

NEXTPNR_NAMESPACE_END
