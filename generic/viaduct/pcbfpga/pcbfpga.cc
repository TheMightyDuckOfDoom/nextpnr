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
#include "mesh_utils.h"

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

        // Replace constants drivers
        h.replace_constants(CellTypePort(id_VCC_DRV, id_ONE), CellTypePort(id_GND_DRV, id_ZERO));
        
        // Remove unused VCC net and driver
        auto& vcc_net  = ctx->nets.at(ctx->id("$PACKER_VCC"));
        if (vcc_net->users.empty()) {
            log_info("pcbFPGA: VCC net has no users\n");
            ctx->nets.erase(vcc_net->name);
            ctx->cells.erase(ctx->id("$PACKER_VCC_DRV"));
            log_info("pcbFPGA: Removed VCC net and driver\n");
        }

        // Remove unused GND net and driver 
        auto& gnd_net  = ctx->nets.at(ctx->id("$PACKER_GND"));
        if (gnd_net->users.empty()) {
            log_info("pcbFPGA: GND net has no users\n");
            ctx->nets.erase(gnd_net->name);
            ctx->cells.erase(ctx->id("$PACKER_GND_DRV"));
            log_info("pcbFPGA: Removed GND net and driver\n");
        }

        // Constrain directly connected LUTs and FFs together to use dedicated resources
        int lutffs = h.constrain_cell_pairs(pool<CellTypePort>{{id_LUT, id_F}}, pool<CellTypePort>{{id_DFF, id_D}}, 1, false);
        log_info("pcbFPGA: Constrained %d LUTFF pairs.\n", lutffs);
        log_info("pcbFPGA: Packing complete.\n");
    }

  private:
    ViaductHelpers h;
    // Configuration
    bool config_loaded = true;
    // Grid size, number of CLBs in each direction -> Mesh is 2 * X + 3 by 2 * Y + 3 tiles
    int X = 32, Y = 32;
    // Lut Size
    int K = 4;

    // Tiles JSON configuration
    json11::Json tiles_json;

    // Parameters that can be referenced in the JSON
    json11::Json params_json;

    // Tile wire map: Maps Bel names to a list of wires
    typedef dict<std::string, std::vector<WireId>> TileWireMap_t;

    std::vector<std::vector<std::string>> tile_types;
    std::vector<std::vector<TileWireMap_t>> wires_per_tile;

    // Get an integer from a JSON object
    int get_json_int(const json11::Json &json, const std::string &key, bool required = false, int def = -1) const {
        // Check if key exists
        if (json[key].is_number()) {
            return json[key].int_value();
        }

        // Throw error if required        
        if (required) {
            log_error("pcbFPGA: JSON missing required parameter '%s'.\n", key.c_str());
        }

        // Return default
        return def;
    }

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
        X = get_json_int(json, "NUM_X_CLB", true);
        Y = get_json_int(json, "NUM_Y_CLB", true);
        K = get_json_int(json, "LUT_SIZE", true);

        // Check config
        if(X < 1 || Y < 1) {
            log_error("pcbFPGA: Invalid num CLBs %d x %d! Must atleast be 1x1!\n", X, Y);
            return;
        }

        // Calculate mesh size
        X = X * 2 + 3;
        Y = Y * 2 + 3;

        // Read out the tile configuration
        if (!json["tiles"].is_array()) {
            log_error("pcbFPGA: Config JSON missing required array 'tiles'.\n");
            return;
        }
        tiles_json = json["tiles"];

        params_json = json11::Json();
        if (json["parameters"].is_object()) {
            log_info("pcbFPGA: Found parameters in JSON\n");
            params_json = json["parameters"];
        }

        config_loaded = true;
    }

    // Print configuration
    void print_config() {
        log_info("pcbFPGA: Configuration:\n");
        log_info("pcbFPGA: \tNum X Tiles: %d\n", X);
        log_info("pcbFPGA: \tNum Y Tiles: %d\n", Y);
        log_info("pcbFPGA: \tLUT Size: %d\n", K);
    }

    json11::Json find_tile_json(const std::string &tile_type, bool required = true)
    {
        for (const auto &tile : tiles_json.array_items()) {
            if (tile["tile_type"].string_value() == tile_type) {
                return tile;
            }
        }
        if (required) {
            log_error("pcbFPGA: Config JSON missing required %s tile.\n", tile_type.c_str());
        }
        return json11::Json();
    }
    // Create Tiles
    void init_tiles()
    {
        // Mesh must be at least 5x5 -> IOBs on each side and one CLB in the middle
        NPNR_ASSERT(X >= 5);
        NPNR_ASSERT(Y >= 5);

        json11::Json iob_tile = find_tile_json("IOB");
        json11::Json clb_tile = find_tile_json("CLB");
        json11::Json icb_tile = find_tile_json("icb");
        json11::Json ccb_tile = find_tile_json("ccb");

        log_info("pcbFPGA: Creating tiles...\n");
        // IOB: Input Output Buffers -> Next to ICBs
        int iob_count = 0;
        // CLB: Configurable Logic Blocks -> Surrounded by 4 Connection Boxes
        int clb_count = 0;
        // QSB: Quad Switch Boxes -> Surrounded by 4 Connection Boxes
        int qsb_count = 0;
        // TSB: Triple Switch Boxes -> Next to 3 Connection Boxes on mesh perimeter
        int tsb_count = 0;
        // DSB: Dual Switch Boxes -> Next to 2 Connection Boxes in mesh corners
        int dsb_count = 0;
        // CCB: CLB Connection Box -> Connects to 2 CLBs and 2 Switch Boxes
        int ccb_count = 0;
        // ICB: IO Connection Box -> Connects to one CLB, one IOB and 2 Switch Boxes
        int icb_count = 0;
        tile_types.resize(Y);
        wires_per_tile.resize(Y);
        for (int y = 0; y < Y; y++) {
            auto& row_tile_types = tile_types.at(y);
            row_tile_types.resize(X);
            wires_per_tile.at(y).resize(X);
            for (int x = 0; x < X; x++) {
                if (mesh_utils::is_io(x, y, X, Y)) {
                    NPNR_ASSERT(row_tile_types.at(x) == "");
                    iob_count++;
                    row_tile_types.at(x) = create_tile_from_json(x, y, iob_tile);
                    continue;
                }
                if(mesh_utils::is_clb(x, y, X, Y)) {
                    NPNR_ASSERT(row_tile_types.at(x) == "");
                    clb_count++;
                    row_tile_types.at(x) = create_tile_from_json(x, y, clb_tile);
                    continue;
                }
                if(mesh_utils::is_qsb(x, y, X, Y)) {
                    NPNR_ASSERT(row_tile_types.at(x) == "");
                    qsb_count++;
                    row_tile_types.at(x) = "qsb";
                    continue;
                }
                if(mesh_utils::is_tsb(x, y, X, Y)) {
                    NPNR_ASSERT(row_tile_types.at(x) == "");
                    tsb_count++;
                    row_tile_types.at(x) = "tsb";
                    continue;
                }
                if(mesh_utils::is_dsb(x, y, X, Y)) {
                    NPNR_ASSERT(row_tile_types.at(x) == "");
                    dsb_count++;
                    row_tile_types.at(x) = "dsb";
                    continue;
                }
                if (mesh_utils::is_ccb(x, y, X, Y)) {
                    NPNR_ASSERT(row_tile_types.at(x) == "");
                    ccb_count++;
                    row_tile_types.at(x) = create_tile_from_json(x, y, ccb_tile);
                    continue;
                }
                if (mesh_utils::is_icb(x, y, X, Y)) {
                    NPNR_ASSERT(row_tile_types.at(x) == "");
                    icb_count++;
                    row_tile_types.at(x) = create_tile_from_json(x, y, icb_tile);
                }
            }
        }

        // Print mesh
        log_info("pcbFPGA: Mesh Summary:\n");
        log_info("pcbFPGA: \tIOBs: %d\n", iob_count);
        log_info("pcbFPGA: \tCLBs: %d\n", clb_count);
        log_info("pcbFPGA: \tQSBs: %d\n", qsb_count);
        log_info("pcbFPGA: \tTSBs: %d\n", tsb_count);
        log_info("pcbFPGA: \tDSBs: %d\n", dsb_count);
        log_info("pcbFPGA: \tCCBs: %d\n", ccb_count);
        log_info("pcbFPGA: \tICBs: %d\n", icb_count);
        log_info("pcbFPGA:      ");
        for(int x = 0; x < X; x++)
           log("%3d ", x);
        log("\n");
        for(int y = 0; y < Y; y++) {
            log_info("pcbFPGA: %3d: ", y);
            auto& row_tile_types = tile_types.at(y);
            for(int x = 0; x < X; x++) {
                std::string tile_type = row_tile_types.at(x);
                if (tile_type == "")
                    log("    "); 
                else
                    log("%s ", tile_type.c_str());
            }
            log("\n");
            log_info("pcbFPGA:\n");
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

            json11::Json start_param = lookup_param(str_start);
            json11::Json end_param = lookup_param(str_end);
            if(start_param.is_number()){
                start = start_param.int_value();
            } else {
                start = std::stoi(str_start);
            }
            if(end_param.is_number()){
                end = end_param.int_value();
            } else {
                end = std::stoi(str_end);
            }
            return name.substr(0, name.find("["));
        }
        start = -1;
        end = -1;
        return name;
    }

    // Lookup parameters in the JSON
    json11::Json lookup_param(const std::string parameter) {
        if (params_json.is_object() && params_json[parameter] != json11::Json()) {
            return params_json[parameter];
        }
        return json11::Json();
    }

    json11::Json lookup_param(const json11::Json parameter) {
        if (parameter.is_string() && params_json.is_object() && params_json[parameter.string_value()] != json11::Json()) {
            return params_json[parameter.string_value()];
        }
        return parameter;
    }

    void create_bel_inout_wire(int x, int y, const std::string& inout_name, const std::string& bel_name, BelId b, TileWireMap_t& tile_wires, const PortType io_type) {
        int start, end;
        const std::string name = parse_name_range(inout_name, end, start);

        if ((start == -1) || (end == -1)) {
            log_info("pcbFPGA: \t\t\tAdding %s to BEL %s\n", name.c_str(), bel_name.c_str());

            IdString input_id = ctx->id(name);
            std::string wire_name = bel_name + "_" + name;
            WireId wire = ctx->addWire(h.xy_id(x, y, ctx->id(wire_name)), input_id, x, y);
            tile_wires[bel_name].push_back(wire);

            ctx->addBelPin(b, input_id, wire, io_type);
        } else {
            for (int i = start; i <= end; i++) {
                log_info("pcbFPGA: \t\t\tAdding %s[%d] to BEL %s\n", name.c_str(), i, bel_name.c_str());

                std::string input_name = name + "[" + std::to_string(i) + "]";
                IdString input_id = ctx->id(input_name);
                std::string wire_name = bel_name + "_" + input_name;
                WireId wire = ctx->addWire(h.xy_id(x, y, ctx->id(wire_name)), input_id, x, y);
                tile_wires[bel_name].push_back(wire);

                ctx->addBelPin(b, input_id, wire, io_type);
            }
        }
    }

    // Create Tile (BEL and Wires) from JSON configuration
    std::string create_tile_from_json(int x, int y, json11::Json tile_json) {
        log_info("pcbFPGA: Creating tile at (%d, %d)\n", x, y);

        // Create BELs
        int bel_count = 0;
        for (const auto &bel_json : tile_json["bels"].array_items()) {
            // Get config
            const int num_per_tile = lookup_param(bel_json["num_per_tile"]).int_value();
            const std::string bel_name = bel_json["name"].string_value();

            // Get the tile wires
            auto& tile_wires = wires_per_tile.at(y).at(x);

            log_info("pcbFPGA: \tCreating %d %s BELs at (%d, %d)\n", num_per_tile, bel_name.c_str(), x, y);

            // ID of BEL type -> Same for all BELs of this type
            IdString bel_type_id = ctx->idf("%s", bel_name.c_str());

            // Create num_per_tile BELs
            for (int z = bel_count; z < bel_count + num_per_tile; z++) {
                // Unique BEL ID at this location
                std::string unique_bel_name = bel_name + std::to_string(z);
                IdString unique_bel_id = ctx->id(unique_bel_name);

                log_info("pcbFPGA: \t\tCreating BEL %s%d at (%d, %d, %d)\n", bel_name.c_str(), z, x, y, z);

                // Create BEL
                BelId b = ctx->addBel(h.xy_id(x, y, unique_bel_id), bel_type_id, Loc(x, y, z), false, false);

                // Add Inputs
                if (bel_json["inputs"].is_array()) {
                    for (const auto &input : bel_json["inputs"].array_items()) {
                       create_bel_inout_wire(x, y, input.string_value(), unique_bel_name, b, tile_wires, PORT_IN);
                    }
                }

                // Add Inouts
                if (bel_json["inouts"].is_array()) {
                    for (const auto &inout : bel_json["inouts"].array_items()) {
                        create_bel_inout_wire(x, y, inout.string_value(), unique_bel_name, b, tile_wires, PORT_INOUT);
                    }
                }

                // Add Outputs
                if (bel_json["outputs"].is_array()) {
                    for (const auto &output : bel_json["outputs"].array_items()) {
                        create_bel_inout_wire(x, y, output.string_value(), unique_bel_name, b, tile_wires, PORT_OUT);
                    }
                }
            }
            bel_count += num_per_tile;
        }

        return tile_json["tile_type"].string_value();
    }

    // Create PIPs
    void init_pips() {
        log_info("pcbFPGA: Creating PIPs...\n");
        for(int y = 0; y < Y; y++) {
            auto& row_tile_types = tile_types.at(y);
            for(int x = 0; x < X; x++) {
                auto tile_json = find_tile_json(row_tile_types.at(x), false);
                if (tile_json != json11::Json()) {
                    log_info("pcbFPGA: Creating PIPs for tile %s at (%d, %d)\n", tile_json["tile_type"].string_value().c_str(), x, y);
                    for (const auto& pip_json : tile_json["pips"].array_items()) {
                        create_pips_for_tile(x, y, pip_json, false);
                    }
                    for (const auto& pip_json : tile_json["internal_pips"].array_items()) {
                        create_pips_for_tile(x, y, pip_json, true);
                    }
                }
            }
        }
    }

    // Create PIPs for a tile
    void create_pips_for_tile(int x, int y, json11::Json pip_json, bool internal) {
        // Get Tile names
        const std::string src_tile_name = pip_json["src_tile"].string_value();
        const std::string dst_tile_name = pip_json["dst_tile"].string_value();

        // Get Wire names
        const std::string src_wire_name = pip_json["src_wire"].string_value();
        const std::string dst_wire_name = pip_json["dst_wire"].string_value();

        // Get Delay
        const delay_t delay = std::max((float) lookup_param(pip_json["delay"]).number_value(), 0.01f);

        // Find the source and destination tiles and bel wires
        auto src_wires_per_bel = find_wires_for_tile_neighbour(x, y, src_tile_name);
        auto dst_wires_per_bel = find_wires_for_tile_neighbour(x, y, dst_tile_name);


        // Filter out the source wires
        auto src_wires = filter_bel_wires(src_wires_per_bel, src_wire_name);
        auto dst_wires = filter_bel_wires(dst_wires_per_bel, dst_wire_name);

        for(const auto& src_wire : src_wires) {
            std::string src_wire_name = ctx->nameOfWire(src_wire);
            std::string src_x_y = src_wire_name.substr(0, src_wire_name.find_last_of("/"));
            for(const auto& dst_wire : dst_wires) {
                std::string dst_wire_name = ctx->nameOfWire(dst_wire);
                std::string dst_x_y = dst_wire_name.substr(0, dst_wire_name.find_last_of("/"));

                if (!internal && (src_x_y == dst_x_y)) {
                    log_info("pcbFPGA: \tSkipping PIP from %s to %s with delay %f\n", ctx->nameOfWire(src_wire), ctx->nameOfWire(dst_wire), delay);
                    continue;
                }
                log_info("pcbFPGA: \tCreating PIP from %s to %s with delay %f\n", ctx->nameOfWire(src_wire), ctx->nameOfWire(dst_wire), delay);
                add_pip(Loc(x, y, 0), src_wire, dst_wire, delay);
            }
        }
    }

    std::vector<WireId> filter_bel_wires(const TileWireMap_t& bel_wires, const std::string& wire_name) {
        std::vector<WireId> wires;
        for(const auto& bel_wire : bel_wires) {
            log_info("pcbFPGA: bel %s\n", bel_wire.first.c_str());
            for(const auto& wire : bel_wire.second) {
                // Get Hierarchical Name
                std::string hier_wire_name = ctx->nameOfWire(wire);
                // Get the wire name
                std::string bel_wire_name = hier_wire_name.substr(hier_wire_name.find_last_of("_") + 1);
                if(bel_wire_name.substr(0, bel_wire_name.find_first_of("[")) == wire_name) {
                    wires.push_back(wire);
                }
            }
        }
        return wires;
    }

    // Find BELs for a tile
    TileWireMap_t find_wires_for_tile_neighbour(int x, int y, const std::string& tile_name) {
        TileWireMap_t wires;
        // Check tile itself
        if (tile_types.at(y).at(x) == tile_name) {
            log_info("pcbFPGA: Found wires for same tile %s at (%d, %d)\n", tile_name.c_str(), x, y);
            for (auto& wire_dict_pair : wires_per_tile.at(y).at(x)) {
                for (const auto& wire : wire_dict_pair.second) {
                    wires[wire_dict_pair.first].push_back(wire);
                }
            }
        }
        // Check top neighbour
        if ((y > 0) && (tile_types.at(y - 1).at(x) == tile_name)) {
            log_info("pcbFPGA: Found wires for tile top neighbor %s at (%d, %d)\n", tile_name.c_str(), x, y - 1);
            for (auto& wire_dict_pair : wires_per_tile.at(y - 1).at(x)) {
                for (const auto& wire : wire_dict_pair.second) {
                    wires[wire_dict_pair.first].push_back(wire);
                }
            }
        }
        // Check bottom neighbour
        if ((y < (Y - 1)) && (tile_types.at(y + 1).at(x) == tile_name)) {
            log_info("pcbFPGA: Found wires for tile bottom neighbor %s at (%d, %d)\n", tile_name.c_str(), x, y + 1);
            for (auto& wire_dict_pair : wires_per_tile.at(y + 1).at(x)) {
                for (const auto& wire : wire_dict_pair.second) {
                    wires[wire_dict_pair.first].push_back(wire);
                }
            }
        }
        // Check left neighbour
        if ((x > 0) && (tile_types.at(y).at(x - 1) == tile_name)) {
            log_info("pcbFPGA: Found wires for tile left neighbor %s at (%d, %d)\n", tile_name.c_str(), x - 1, y);
            for (auto& wire_dict_pair : wires_per_tile.at(y).at(x - 1)) {
                for (const auto& wire : wire_dict_pair.second) {
                    wires[wire_dict_pair.first].push_back(wire);
                }
            }
        }
        // Check right neighbour
        if ((x < (X - 1)) && (tile_types.at(y).at(x + 1) == tile_name)) {
            log_info("pcbFPGA: Found wires for tile right neighbor %s at (%d, %d)\n", tile_name.c_str(), x + 1, y);
            for (auto& wire_dict_pair : wires_per_tile.at(y).at(x + 1)) {
                for (const auto& wire : wire_dict_pair.second) {
                    wires[wire_dict_pair.first].push_back(wire);
                }
            }
        }
        return wires;
    }

    PipId add_pip(Loc loc, WireId src, WireId dst, delay_t delay)
    {
        IdStringList name = IdStringList::concat(ctx->getWireName(dst), ctx->getWireName(src));
        return ctx->addPip(name, ctx->id("PIP"), src, dst, delay, loc);
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
