/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2021  gatecat <gatecat@ds0.me>
 *  Copyright (C) 2022  Lofty <dan.ravensloft@gmail.com>
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
#include <fstream>

#include "json11.hpp"
#include "log.h"
#include "nextpnr.h"
#include "util.h"
#include "viaduct_api.h"
#include "viaduct_helpers.h"

#define GEN_INIT_CONSTIDS
#define VIADUCT_CONSTIDS "viaduct/pcbfpga/constids.inc"
#include "viaduct_constids.h"

NEXTPNR_NAMESPACE_BEGIN

namespace {
struct PcbfpgaImpl : ViaductAPI
{
    PcbfpgaImpl() {}
    PcbfpgaImpl(const dict<std::string, std::string> &args) {
        if (args.empty()) {
            log_error("pcbFPGA: No arguments provided to PcbfpgaImpl\n");
        }

        for (const auto &arg : args) {
            if (arg.first == "config") {
                load_json_config(arg.second);
            }
            else {
                log_info("pcbFPGA: Valid arguments are: 'config'\n");
                log_error("pcbFPGA: Unknown argument '%s' for pcbFPGA\n", arg.first.c_str());
            }
        }
    }
    ~PcbfpgaImpl(){};
    void init(Context *ctx) override
    {
        if(!config_loaded) {
            log_error("pcbFPGA: No configuration loaded for pcbFPGA\n");
            return;
        }
        print_config();
        init_uarch_constids(ctx);
        ViaductAPI::init(ctx);
        h.init(ctx);
        init_wires();
        init_tiles();
        init_pips();
        log_info("pcbFPGA: Architecture initialized.\n");
    }

    void pack() override
    {
        log_info("pcbFPGA: Packing...\n");
        // Trim nextpnr IOBs - assume IO buffer insertion has been done in synthesis
        const pool<CellTypePort> top_ports{
                CellTypePort(id_INBUF, id_PAD),
                CellTypePort(id_OUTBUF, id_PAD),
        };
        h.remove_nextpnr_iobs(top_ports);
        // Replace constants with LUTs
        const dict<IdString, Property> vcc_params = {{id_INIT, Property((1 << (1 << K)) - 1, (1 << K))}};
        const dict<IdString, Property> gnd_params = {{id_INIT, Property(0, 1 << K)}};
        h.replace_constants(CellTypePort(id_LUT, id_F), CellTypePort(id_LUT, id_F), vcc_params, gnd_params);
        // Constrain directly connected LUTs and FFs together to use dedicated resources
        int lutffs = h.constrain_cell_pairs(pool<CellTypePort>{{id_LUT, id_F}}, pool<CellTypePort>{{id_DFF, id_D}}, 1,
                                            false);
        log_info("pcbFPGA: Constrained %d LUTFF pairs.\n", lutffs);
        log_info("pcbFPGA: Packing complete.\n");
    }

    void prePlace() override { assign_cell_info(); }

    bool isBelLocationValid(BelId bel, bool explain_invalid) const override
    {
        Loc l = ctx->getBelLocation(bel);
        if (is_io(l.x, l.y)) {
            return true;
        } else {
            return slice_valid(l.x, l.y, l.z / 2);
        }
    }

  private:
    ViaductHelpers h;
    // Configuration
    bool config_loaded = true;
    // Grid size including IOBs at edges
    int X = 32, Y = 32;
    // IOs per tile
    int M = 1;
    // SLICEs per tile
    int N = 1;
    // LUT input count
    int K = 4;
    // Number of tile input buses
    int InputMuxCount = 8; // >= 6 for attosoc; >= 10 for arbiter
    // Number of output wires in a direction
    int OutputMuxCount = 8; // >= 5 for attosoc; >= 8 for arbiter

    // Tiles JSON configuration
    json11::Json tiles_json;

    // Tile 

    // For fast wire lookups
    struct TileWires
    {
        std::vector<WireId> clk, q, f, d;
        std::vector<WireId> slice_inputs;
        std::vector<WireId> slice_outputs;
        std::vector<WireId> tile_inputs_north, tile_inputs_east, tile_inputs_south, tile_inputs_west;
        std::vector<WireId> tile_outputs_north, tile_outputs_east, tile_outputs_south, tile_outputs_west;
        std::vector<WireId> pad;
    };

    typedef dict<std::string, std::vector<WireId>> TileWireMap_t;

    std::vector<std::vector<TileWireMap_t>> wires_per_tile;
    std::vector<std::vector<TileWires>> wires_by_tile;

    // Load Config from json
    void load_json_config(std::string filename) {
        config_loaded = false;

        // Load the JSON file
        json11::Json json;
        std::ifstream json_file(filename.c_str());

        std::string json_str((std::istreambuf_iterator<char>(json_file)), std::istreambuf_iterator<char>());
        log_info("%s\n", json_str.c_str());
        std::string error;
        json = json11::Json::parse(json_str, error, json11::JsonParse::COMMENTS);
        if (json.is_null()) {
            log_error("pcbFPGA: Failed to parse config JSON file '%s': %s.\n", filename.c_str(), error.c_str());
            return;
        }

        // Read out the configuration
        const std::string param_names[] = {"NUM_X_TILES", "NUM_Y_TILES", "IOS_PER_TILE", "SLICES_PER_TILE", "LUT_SIZE"};
        int* params[] = {&X, &Y, &M, &N, &K};
        for (int i = 0; i < 4; i++) {
            if (!json[param_names[i]].is_number()) {
                log_error("pcbFPGA: Config JSON missing required parameter '%s'.\n", param_names[i].c_str());
                return;
            }
            *params[i] = json[param_names[i]].int_value();
        }

        // Read out the tile configuration
        if (!json["tiles"].is_array()) {
            log_error("pcbFPGA: Config JSON missing required array 'tiles'.\n");
            return;
        }
        tiles_json = json["tiles"];

        config_loaded = true;
    }

    // Print configuration
    void print_config() {
        log_info("pcbFPGA: Configuration:\n");
        log_info("pcbFPGA: \tNum X Tiles: %d\n", X);
        log_info("pcbFPGA: \tNum Y Tiles: %d\n", Y);
        log_info("pcbFPGA: \tIOBs per Tile: %d\n", M);
        log_info("pcbFPGA: \tSlices per Tile: %d\n", N);
        log_info("pcbFPGA: \tLUT Size: %d\n", K);
        log_info("pcbFPGA: \tInput Mux Count: %d\n", InputMuxCount);
        log_info("pcbFPGA: \tOutput Mux Count: %d\n", OutputMuxCount);
    }

    // Create wires to attach to bels and pips
    void init_wires()
    {
        NPNR_ASSERT(X >= 3);
        NPNR_ASSERT(Y >= 3);
        NPNR_ASSERT(K >= 2);
        NPNR_ASSERT(M >= 1);
        NPNR_ASSERT(N >= M);
        NPNR_ASSERT(InputMuxCount >= OutputMuxCount);

        log_info("pcbFPGA: Creating wires...\n");
        wires_by_tile.resize(Y);
        wires_per_tile.resize(Y);
        for (int y = 0; y < Y; y++) {
            wires_per_tile.at(y).resize(X);
            auto &row_wires = wires_by_tile.at(y);
            row_wires.resize(X);
            for (int x = 0; x < X; x++) {
                auto &w = row_wires.at(x);
                // Tile inputs
                for (int tile_input = 0; tile_input < InputMuxCount; tile_input++) {
                    w.tile_inputs_north.push_back(
                            ctx->addWire(h.xy_id(x, y, ctx->idf("TILEINN[%d]", tile_input)), ctx->id("TILEINN"), x, y));
                    w.tile_inputs_east.push_back(
                            ctx->addWire(h.xy_id(x, y, ctx->idf("TILEINE[%d]", tile_input)), ctx->id("TILEINE"), x, y));
                    w.tile_inputs_south.push_back(
                            ctx->addWire(h.xy_id(x, y, ctx->idf("TILEINS[%d]", tile_input)), ctx->id("TILEINS"), x, y));
                    w.tile_inputs_west.push_back(
                            ctx->addWire(h.xy_id(x, y, ctx->idf("TILEINW[%d]", tile_input)), ctx->id("TILEINW"), x, y));
                }
                // Tile outputs
                for (int tile_output = 0; tile_output < OutputMuxCount; tile_output++) {
                    w.tile_outputs_north.push_back(ctx->addWire(h.xy_id(x, y, ctx->idf("TILEOUTN[%d]", tile_output)),
                                                                ctx->id("TILEOUTN"), x, y));
                    w.tile_outputs_east.push_back(ctx->addWire(h.xy_id(x, y, ctx->idf("TILEOUTE[%d]", tile_output)),
                                                               ctx->id("TILEOUTE"), x, y));
                    w.tile_outputs_south.push_back(ctx->addWire(h.xy_id(x, y, ctx->idf("TILEOUTS[%d]", tile_output)),
                                                                ctx->id("TILEOUTS"), x, y));
                    w.tile_outputs_west.push_back(ctx->addWire(h.xy_id(x, y, ctx->idf("TILEOUTW[%d]", tile_output)),
                                                               ctx->id("TILEOUTW"), x, y));
                }
            }
        }
    }
    bool is_io(int x, int y) const
    {
        // IO are on the edges of the device
        return (x == 0) || (x == (X - 1)) || (y == 0) || (y == (Y - 1));
    }
    PipId add_pip(Loc loc, WireId src, WireId dst, delay_t delay = 0.05)
    {
        IdStringList name = IdStringList::concat(ctx->getWireName(dst), ctx->getWireName(src));
        return ctx->addPip(name, ctx->id("PIP"), src, dst, delay, loc);
    }
    json11::Json find_tile_json(const std::string &tile_type)
    {
        for (const auto &tile : tiles_json.array_items()) {
            if (tile["tile_type"].string_value() == tile_type) {
                return tile;
            }
        }
        return json11::Json();
    }
    // Create Tiles
    void init_tiles()
    {
        json11::Json io_tile = find_tile_json("io");
        json11::Json sle_tile = find_tile_json("sle");
        if (io_tile.is_null()) {
            log_error("pcbFPGA: Config JSON missing required io tile.\n");
            return;
        }
        if (sle_tile.is_null()) {
            log_error("pcbFPGA: Config JSON missing required sle tile.\n");
            return;
        }

        log_info("pcbFPGA: Creating bels...\n");
        for (int y = 0; y < Y; y++) {
            for (int x = 0; x < X; x++) {
                if (is_io(x, y)) {
                    if (x == y)
                        continue; // don't put IO in corners
                    create_tile_from_json(x, y, io_tile);
                } else {
                    create_tile_from_json(x, y, sle_tile);
                }
            }
        }
    }

    // Parses a name of the form "name[end:start]"
    std::string parse_name_range(const std::string name, int& end, int& start) {
        if ((name.find("[") != std::string::npos)
          && (name.find("]") != std::string::npos)
          && (name.find("]") > name.find("["))
          && (name.find(":") != std::string::npos)) {
           std::string str_end = name.substr(name.find("[") + 1, name.find(":") - 2); 
           std::string str_start = name.substr(name.find(":") + 1, name.find("]")); 
           //log_info("pcbFPGA: Found IO %s with range: %s to %s\n", name.substr(0, name.find("[")).c_str(), str_end.c_str(), str_start.c_str());

           start = std::stoi(str_start);
           end = std::stoi(str_end);
           return name.substr(0, name.find("["));
        }
        start = -1;
        end = -1;
        return name;
    }

    enum BelIoType {
        BEL_INPUT,
        BEL_OUTPUT,
        BEL_INOUT
    };

    void create_bel_inout_wire(int x, int y, int z, const std::string& inout_name, const std::string& bel_name, BelId b, TileWireMap_t& tile_wires, const BelIoType io_type) {
        int start, end;
        const std::string name = parse_name_range(inout_name, end, start);

        if ((start == -1) || (end == -1)) {
            log_info("pcbFPGA: \t\t\tAdding %s to BEL %s%d\n", name.c_str(), bel_name.c_str(), z);

            IdString input_id = ctx->idf("%s", name.c_str());
            WireId wire = ctx->addWire(h.xy_id(x, y, ctx->idf("%s%d", name.c_str(), z)), input_id, x, y);
            tile_wires[name].push_back(wire);

            if (io_type == BEL_INPUT) {
                ctx->addBelInput(b, input_id, wire);
            } else if (io_type == BEL_OUTPUT) {
                ctx->addBelOutput(b, input_id, wire);
            } else {
                ctx->addBelInout(b, input_id, wire);
            }
        } else {
            for (int i = start; i <= end; i++) {
                log_info("pcbFPGA: \t\t\tAdding %s[%d] to BEL %s%d\n", name.c_str(), i, bel_name.c_str(), z);

                IdString input_id = ctx->idf("%s[%d]", name.c_str(), i);
                WireId wire = ctx->addWire(h.xy_id(x, y, ctx->idf("%s[%d]%d", name.c_str(), i, z)), input_id, x, y);
                tile_wires[name].push_back(wire);

                if (io_type == BEL_INPUT) {
                    ctx->addBelInput(b, input_id, wire);
                } else if (io_type == BEL_OUTPUT) {
                    ctx->addBelOutput(b, input_id, wire);
                } else {
                    ctx->addBelInout(b, input_id, wire);
                }
            }
        }
    }

    // Create Tile (BEL and Wires) from JSON configuration
    void create_tile_from_json(int x, int y, json11::Json tile_json) {
        log_info("pcbFPGA: Creating tile at (%d, %d)\n", x, y);

        // Create BELs
        int bel_count = 0;
        for (const auto &bel_json : tile_json["bels"].array_items()) {
            // Get config
            const int num_per_tile = bel_json["num_per_tile"].int_value();
            const std::string bel_name = bel_json["name"].string_value();

            // Get the tile wires
            auto& tile_wires = wires_per_tile.at(y).at(x);

            log_info("pcbFPGA: \tCreating %d %s BELs at (%d, %d)\n", num_per_tile, bel_name.c_str(), x, y);

            // ID of BEL type -> Same for all BELs of this type
            IdString bel_type_id = ctx->idf("%s", bel_name.c_str());

            // Create num_per_tile BELs
            for (int z = bel_count; z < bel_count + num_per_tile; z++) {
                // Unique BEL ID at this location
                IdString unique_bel_id = ctx->idf("%s%d", bel_name.c_str(), z);

                log_info("pcbFPGA: \t\tCreating BEL %s%d at (%d, %d, %d)\n", bel_name.c_str(), z, x, y, z);

                // Create BEL
                BelId b = ctx->addBel(h.xy_id(x, y, unique_bel_id), bel_type_id, Loc(x, y, z), false, false);

                // Add Inputs
                if (bel_json["inputs"].is_array()) {
                    for (const auto &input : bel_json["inputs"].array_items()) {
                       create_bel_inout_wire(x, y, z, input.string_value(), bel_name, b, tile_wires, BEL_INPUT);
                    }
                }

                // Add Inouts
                if (bel_json["inouts"].is_array()) {
                    for (const auto &inout : bel_json["inouts"].array_items()) {
                        create_bel_inout_wire(x, y, z, inout.string_value(), bel_name, b, tile_wires, BEL_INOUT);
                    }
                }

                // Add Outputs
                if (bel_json["outputs"].is_array()) {
                    for (const auto &output : bel_json["outputs"].array_items()) {
                        create_bel_inout_wire(x, y, z, output.string_value(), bel_name, b, tile_wires, BEL_OUTPUT);
                    }
                }
            }
            bel_count += num_per_tile;
        }
    }

    // Create PIPs inside a tile; following an example synthetic routing pattern
    void add_io_pips(int x, int y)
    {
        auto &w = wires_by_tile.at(y).at(x);
        Loc loc(x, y, 0);

        const uint16_t tile_input_config[8] = {
                0b0000'0000'0000'0001, 0b0000'0000'0000'0001, 0b0000'0000'0000'0001, 0b0000'0000'0000'0001,
                0b0000'0000'0000'0010, 0b0000'0000'0000'0010, 0b0000'0000'0000'0010, 0b0000'0000'0000'0010,
        };

        // Tile inputs
        for (int tile_input = 0; tile_input < InputMuxCount; tile_input++) {
            auto &dst = w.tile_inputs_north.at(tile_input);
            // North
            for (int step = 1; step <= 4; step++) {
                if (y - step <= 0 || x == 0 || x == X - 1)
                    break;
                auto &w = wires_by_tile.at(y - step).at(x);
                for (int tile_output = 0; tile_output < OutputMuxCount; tile_output++)
                    if ((1 << tile_input) & tile_input_config[tile_output])
                        add_pip(loc, w.tile_outputs_north.at(tile_output), dst);
            }
        }

        for (int tile_input = 0; tile_input < InputMuxCount; tile_input++) {
            auto &dst = w.tile_inputs_east.at(tile_input);
            // East
            for (int step = 1; step <= 4; step++) {
                if (x - step <= 0 || y == 0 || y == Y - 1)
                    break;
                auto &w = wires_by_tile.at(y).at(x - step);
                for (int tile_output = 0; tile_output < OutputMuxCount; tile_output++)
                    if ((1 << tile_input) & tile_input_config[tile_output])
                        add_pip(loc, w.tile_outputs_east.at(tile_output), dst);
            }
        }

        for (int tile_input = 0; tile_input < InputMuxCount; tile_input++) {
            auto &dst = w.tile_inputs_south.at(tile_input);
            // South
            for (int step = 1; step <= 4; step++) {
                if (y + step >= Y || x == 0 || x == X - 1)
                    break;
                auto &w = wires_by_tile.at(y + step).at(x);
                for (int tile_output = 0; tile_output < OutputMuxCount; tile_output++)
                    if ((1 << tile_input) & tile_input_config[tile_output])
                        add_pip(loc, w.tile_outputs_south.at(tile_output), dst);
            }
        }

        for (int tile_input = 0; tile_input < InputMuxCount; tile_input++) {
            auto &dst = w.tile_inputs_west.at(tile_input);
            // West
            for (int step = 1; step <= 4; step++) {
                if (x + step >= X || y == 0 || y == Y - 1)
                    break;
                auto &w = wires_by_tile.at(y).at(x + step);
                for (int tile_output = 0; tile_output < OutputMuxCount; tile_output++)
                    if ((1 << tile_input) & tile_input_config[tile_output])
                        add_pip(loc, w.tile_outputs_west.at(tile_output), dst);
            }
        }

        // Tile outputs
        for (int tile_output = 0; tile_output < OutputMuxCount; tile_output++) {
            for (int z = 0; z < M; z++) {
                WireId src = w.slice_outputs.at(z);
                // O output
                if (y == 0)
                    add_pip(loc, src, w.tile_outputs_north.at(tile_output));
                if (x == 0)
                    add_pip(loc, src, w.tile_outputs_east.at(tile_output));
                if (y == Y - 1)
                    add_pip(loc, src, w.tile_outputs_south.at(tile_output));
                if (x == X - 1)
                    add_pip(loc, src, w.tile_outputs_west.at(tile_output));
            }
        }

        // Pad inputs
        for (const auto &src : w.tile_inputs_north) {
            for (int z = 0; z < M; z++) {
                // I input
                add_pip(loc, src, w.slice_inputs.at(z * K + 0));
                // EN input
                add_pip(loc, src, w.slice_inputs.at(z * K + 1));
            }
        }
        for (const auto &src : w.tile_inputs_east) {
            for (int z = 0; z < M; z++) {
                // I input
                add_pip(loc, src, w.slice_inputs.at(z * K + 0));
                // EN input
                add_pip(loc, src, w.slice_inputs.at(z * K + 1));
            }
        }
        for (const auto &src : w.tile_inputs_south) {
            for (int z = 0; z < M; z++) {
                // I input
                add_pip(loc, src, w.slice_inputs.at(z * K + 0));
                // EN input
                add_pip(loc, src, w.slice_inputs.at(z * K + 1));
            }
        }
        for (const auto &src : w.tile_inputs_west) {
            for (int z = 0; z < M; z++) {
                // I input
                add_pip(loc, src, w.slice_inputs.at(z * K + 0));
                // EN input
                add_pip(loc, src, w.slice_inputs.at(z * K + 1));
            }
        }
    }
    void add_slice_pips(int x, int y)
    {
        auto &w = wires_by_tile.at(y).at(x);
        Loc loc(x, y, 0);

        const uint16_t tile_input_config[8] = {0b1010'1010'1010'1010, 0b0101'0101'0101'0101, 0b0110'0110'0110'0110,
                                               0b1001'1001'1001'1001, 0b0011'0011'0011'0011, 0b1100'1100'1100'1100,
                                               0b1111'0000'1111'0000, 0b0000'1111'0000'1111};

        // Slice input selector
        for (int lut = 0; lut < N; lut++) {
            for (int lut_input = 0; lut_input < K; lut_input++) {
                for (const auto &tile_input : w.tile_inputs_north) // Tile input bus
                    add_pip(loc, tile_input, w.slice_inputs.at(lut * K + lut_input));
                for (const auto &tile_input : w.tile_inputs_east) // Tile input bus
                    add_pip(loc, tile_input, w.slice_inputs.at(lut * K + lut_input));
                for (const auto &tile_input : w.tile_inputs_south) // Tile input bus
                    add_pip(loc, tile_input, w.slice_inputs.at(lut * K + lut_input));
                for (const auto &tile_input : w.tile_inputs_west) // Tile input bus
                    add_pip(loc, tile_input, w.slice_inputs.at(lut * K + lut_input));
                for (const auto &slice_output : w.slice_outputs) // Slice output bus
                    add_pip(loc, slice_output, w.slice_inputs.at(lut * K + lut_input));
            }
            for (const auto &tile_input : w.tile_inputs_north) // Clock selector
                add_pip(loc, tile_input, w.clk.at(lut));
            for (const auto &tile_input : w.tile_inputs_east) // Clock selector
                add_pip(loc, tile_input, w.clk.at(lut));
            for (const auto &tile_input : w.tile_inputs_south) // Clock selector
                add_pip(loc, tile_input, w.clk.at(lut));
            for (const auto &tile_input : w.tile_inputs_west) // Clock selector
                add_pip(loc, tile_input, w.clk.at(lut));
        }

        // Slice output selector
        for (int slice_output = 0; slice_output < N; slice_output++) {
            add_pip(loc, w.f.at(slice_output), w.slice_outputs.at(slice_output)); // LUT output
            add_pip(loc, w.q.at(slice_output), w.slice_outputs.at(slice_output)); // DFF output
        }

        // Tile input selector
        for (int step = 1; step <= 4; step++) {
            if (y + step < Y) // South
                for (size_t tile_input_index = 0; tile_input_index < w.tile_inputs_north.size(); tile_input_index++)
                    for (size_t tile_output_index = 0;
                         tile_output_index < wires_by_tile.at(y + step).at(x).tile_outputs_south.size();
                         tile_output_index++)
                        if ((1 << tile_input_index) & tile_input_config[tile_output_index])
                            add_pip(loc, wires_by_tile.at(y + step).at(x).tile_outputs_south.at(tile_output_index),
                                    w.tile_inputs_north.at(tile_input_index));

            if (x + step < X) // West
                for (size_t tile_input_index = 0; tile_input_index < w.tile_inputs_east.size(); tile_input_index++)
                    for (size_t tile_output_index = 0;
                         tile_output_index < wires_by_tile.at(y).at(x + step).tile_outputs_west.size();
                         tile_output_index++)
                        if ((1 << tile_input_index) & tile_input_config[tile_output_index])
                            add_pip(loc, wires_by_tile.at(y).at(x + step).tile_outputs_west.at(tile_output_index),
                                    w.tile_inputs_east.at(tile_input_index));

            if (y - step >= 0) // North
                for (size_t tile_input_index = 0; tile_input_index < w.tile_inputs_south.size(); tile_input_index++)
                    for (size_t tile_output_index = 0;
                         tile_output_index < wires_by_tile.at(y - step).at(x).tile_outputs_north.size();
                         tile_output_index++)
                        if ((1 << tile_input_index) & tile_input_config[tile_output_index])
                            add_pip(loc, wires_by_tile.at(y - step).at(x).tile_outputs_north.at(tile_output_index),
                                    w.tile_inputs_south.at(tile_input_index));

            if (x - step >= 0) // East
                for (size_t tile_input_index = 0; tile_input_index < w.tile_inputs_west.size(); tile_input_index++)
                    for (size_t tile_output_index = 0;
                         tile_output_index < wires_by_tile.at(y).at(x - step).tile_outputs_east.size();
                         tile_output_index++)
                        if ((1 << tile_input_index) & tile_input_config[tile_output_index])
                            add_pip(loc, wires_by_tile.at(y).at(x - step).tile_outputs_east.at(tile_output_index),
                                    w.tile_inputs_west.at(tile_input_index));
        }

        // Tile output selector
        for (const auto &slice_output : w.slice_outputs) {
            for (const auto &tile_output : w.tile_outputs_north)
                add_pip(loc, slice_output, tile_output);
            for (const auto &tile_output : w.tile_outputs_east)
                add_pip(loc, slice_output, tile_output);
            for (const auto &tile_output : w.tile_outputs_south)
                add_pip(loc, slice_output, tile_output);
            for (const auto &tile_output : w.tile_outputs_west)
                add_pip(loc, slice_output, tile_output);
        }

        for (const auto &tile_input : w.tile_inputs_north) {
            for (const auto &tile_output : w.tile_outputs_north)
                add_pip(loc, tile_input, tile_output);
            for (const auto &tile_output : w.tile_outputs_east)
                add_pip(loc, tile_input, tile_output);
            for (const auto &tile_output : w.tile_outputs_south)
                add_pip(loc, tile_input, tile_output);
            for (const auto &tile_output : w.tile_outputs_west)
                add_pip(loc, tile_input, tile_output);
        }
        for (const auto &tile_input : w.tile_inputs_east) {
            for (const auto &tile_output : w.tile_outputs_north)
                add_pip(loc, tile_input, tile_output);
            for (const auto &tile_output : w.tile_outputs_east)
                add_pip(loc, tile_input, tile_output);
            for (const auto &tile_output : w.tile_outputs_south)
                add_pip(loc, tile_input, tile_output);
            for (const auto &tile_output : w.tile_outputs_west)
                add_pip(loc, tile_input, tile_output);
        }
        for (const auto &tile_input : w.tile_inputs_south) {
            for (const auto &tile_output : w.tile_outputs_north)
                add_pip(loc, tile_input, tile_output);
            for (const auto &tile_output : w.tile_outputs_east)
                add_pip(loc, tile_input, tile_output);
            for (const auto &tile_output : w.tile_outputs_south)
                add_pip(loc, tile_input, tile_output);
            for (const auto &tile_output : w.tile_outputs_west)
                add_pip(loc, tile_input, tile_output);
        }
        for (const auto &tile_input : w.tile_inputs_west) {
            for (const auto &tile_output : w.tile_outputs_north)
                add_pip(loc, tile_input, tile_output);
            for (const auto &tile_output : w.tile_outputs_east)
                add_pip(loc, tile_input, tile_output);
            for (const auto &tile_output : w.tile_outputs_south)
                add_pip(loc, tile_input, tile_output);
            for (const auto &tile_output : w.tile_outputs_west)
                add_pip(loc, tile_input, tile_output);
        }
    }
    void init_pips()
    {
        log_info("pcbFPGA: Creating pips...\n");
        for (int y = 0; y < Y; y++)
            for (int x = 0; x < X; x++) {
                if (is_io(x, y)) {
                    add_io_pips(x, y);
                } else {
                    add_slice_pips(x, y);
                }
            }
    }
    // Validity checking
    struct PcbfpgaCellInfo
    {
        const NetInfo *lut_f = nullptr, *ff_d = nullptr;
        bool lut_i3_used = false;
    };
    std::vector<PcbfpgaCellInfo> fast_cell_info;
    void assign_cell_info()
    {
        fast_cell_info.resize(ctx->cells.size());
        for (auto &cell : ctx->cells) {
            CellInfo *ci = cell.second.get();
            auto &fc = fast_cell_info.at(ci->flat_index);
            if (ci->type == id_LUT) {
                fc.lut_f = ci->getPort(id_F);
                fc.lut_i3_used = (ci->getPort(ctx->idf("I[%d]", K - 1)) != nullptr);
            } else if (ci->type == id_DFF) {
                fc.ff_d = ci->getPort(id_D);
            }
        }
    }
    bool slice_valid(int x, int y, int z) const
    {
        const CellInfo *lut = ctx->getBoundBelCell(ctx->getBelByLocation(Loc(x, y, z * 2)));
        const CellInfo *ff = ctx->getBoundBelCell(ctx->getBelByLocation(Loc(x, y, z * 2 + 1)));
        if (!lut || !ff)
            return true; // always valid if only LUT or FF used
        const auto &lut_data = fast_cell_info.at(lut->flat_index);
        const auto &ff_data = fast_cell_info.at(ff->flat_index);
        // In our example arch; the FF D can either be driven from LUT F or LUT I3
        // so either; FF D must equal LUT F or LUT I3 must be unused
        if ((ff_data.ff_d == lut_data.lut_f && lut_data.lut_f->users.entries() == 1) || !lut_data.lut_i3_used)
            return true;
        // Can't route FF and LUT output separately
        return false;
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
};

struct PcbfpgaArch : ViaductArch
{
    PcbfpgaArch() : ViaductArch("pcbfpga"){};
    std::unique_ptr<ViaductAPI> create(const dict<std::string, std::string> &args)
    {
        if (!args.empty()) {
            return std::make_unique<PcbfpgaImpl>(args);
        }
        return std::make_unique<PcbfpgaImpl>();
    }
} pcbfpgaArch;
} // namespace

NEXTPNR_NAMESPACE_END
