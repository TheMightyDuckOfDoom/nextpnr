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
        print_mesh();
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
        remove_nextpnr_iobs(top_ports);

        // Replace constants drivers
        h.replace_constants(CellTypePort(id_VCC_DRV, id_ONE), CellTypePort(id_GND_DRV, id_ZERO));
        
        // Remove unused VCC net and driver
        auto& vcc_net  = ctx->nets.at(ctx->id("$PACKER_VCC"));
        if (vcc_net->users.empty()) {
            log_info("pcbFPGA: VCC net has no users\n");
            ctx->nets.erase(vcc_net->name);
            ctx->cells.erase(ctx->id("$PACKER_VCC_DRV"));
            log_info("pcbFPGA: Removed VCC net and driver\n");
        } else {
            log_info("pcbFPGA: VCC Driver %s\n", vcc_net->driver.cell->type.c_str(ctx));
            int count = 0;
            for(auto& user : vcc_net->users) {
                log_info("pcbFPGA: VCC net has user %s:%s %s\n", user.cell->type.c_str(ctx), user.port.c_str(ctx), user.cell->name.c_str(ctx));
                count++;
            }
            log_info("pcbFPGA: VCC net has %d users\n", count);
        }

        // Remove unused GND net and driver 
        auto& gnd_net  = ctx->nets.at(ctx->id("$PACKER_GND"));
        if (gnd_net->users.empty()) {
            log_info("pcbFPGA: GND net has no users\n");
            ctx->nets.erase(gnd_net->name);
            ctx->cells.erase(ctx->id("$PACKER_GND_DRV"));
            log_info("pcbFPGA: Removed GND net and driver\n");
        } else {
            log_info("pcbFPGA: GND Driver %s\n", gnd_net->driver.cell->type.c_str(ctx));
            int count = 0;
            for(auto& user : gnd_net->users) {
                log_info("pcbFPGA: GND net has user %s:%s %s\n", user.cell->type.c_str(ctx), user.port.c_str(ctx), user.cell->name.c_str(ctx));
                count++;
            }
            log_info("pcbFPGA: GND net has %d users\n", count);
        }

        // Check if LUTs have full width
        IdString lut_id = ctx->id("PCBFPGA_LUT");
        IdString k_id = ctx->id("K");
        for (const auto& cell : ctx->cells) {
            const auto& ci = cell.second;
            if (ci->type == lut_id) {
                // Check if LUT has full width
                int lut_k = ci->params[k_id].as_int64();
                if (lut_k != K) {
                    log_error("pcbFPGA: LUT %s has width %d, expected %d\n", ci->name.c_str(ctx), lut_k, K);
                }
            }
        }

        // Constrain Cell Pairs
        for(auto& pair : constrain_cell_pairs) {
            // Get Ports
            int driver_start, driver_end;
            int user_start, user_end;
            std::string driver_port = parse_name_range(pair.driver_port, driver_end, driver_start);
            std::string user_port   = parse_name_range(pair.user_port,   user_end,   user_start);

            // Get BELs
            IdString driver_bel_id = ctx->id(pair.driver_bel);
            IdString user_bel_id   = ctx->id(pair.user_bel);

            // Constrain Driver Bel:Driver Port to User Bel:User Port
            if((driver_start == driver_end) && (user_start == user_end)) {
                // One Driver Port, One User Port
                IdString user_port_id = ctx->idf("%s", user_port.c_str());
                int count = h.constrain_cell_pairs(pool<CellTypePort>{{driver_bel_id, ctx->id(driver_port)}}, pool<CellTypePort>{{user_bel_id, user_port_id}}, 1, false);
                log_info("pcbFPGA: Constrained single driver %d %s:%s to single user %s:%s\n", count, pair.driver_bel.c_str(), driver_port.c_str(), pair.user_bel.c_str(), user_port.c_str());
            }
            else {
                log_error("pcbFPGA: Constrained ranges not supported yet\n");
            }
        }

        log_info("pcbFPGA: Packing complete.\n");
    }

    void prePlace() override
    {
        log_info("pcbFPGA: Pre-placing...\n");
        add_cell_timing();
        log_info("pcbFPGA: Pre-placing complete.\n");
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

    typedef struct {
        std::string driver_bel;
        std::string driver_port;
        std::string user_bel;
        std::string user_port;
    } ConstrainCellPair_t;

    std::vector<ConstrainCellPair_t> constrain_cell_pairs;
    std::vector<std::vector<std::string>> tile_types;
    std::vector<std::vector<TileWireMap_t>> wires_per_tile;

    dict<IdString, json11::Json> bel_type_id_to_json;

    void remove_nextpnr_iobs(const pool<CellTypePort> &top_ports)
    {
        std::vector<IdString> to_remove;
        for (auto &cell : ctx->cells) {
            auto &ci = *cell.second;
            if (!ci.type.in(ctx->id("$nextpnr_ibuf"), ctx->id("$nextpnr_obuf"), ctx->id("$nextpnr_iobuf")))
                continue;
            NetInfo *i = ci.getPort(ctx->id("I"));
            if (i && i->driver.cell) {
                if (!top_ports.count(CellTypePort(i->driver)))
                    log_error("Top-level port '%s' driven by illegal port %s.%s\n", ctx->nameOf(&ci),
                            ctx->nameOf(i->driver.cell), ctx->nameOf(i->driver.port));
            }
            NetInfo *o = ci.getPort(ctx->id("O"));
            if (o) {
                for (auto &usr : o->users) {
                    if (!top_ports.count(CellTypePort(usr)) && usr.port != id_PAD)
                        log_error("Top-level port '%s' driving illegal port %s.%s\n", ctx->nameOf(&ci),
                                ctx->nameOf(usr.cell), ctx->nameOf(usr.port));
                }
            }
            ci.disconnectPort(ctx->id("I"));
            ci.disconnectPort(ctx->id("O"));
            to_remove.push_back(ci.name);
        }
        for (IdString cell_name : to_remove)
            ctx->cells.erase(cell_name);
    }

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
    // Print mesh
    void print_mesh() {
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
    // Parse Constrain Cell Pairs from JSON Tiles
    void parse_constrain_cell_pairs() {
        // Loop over Tiles
        for(auto& tile_json : tiles_json.array_items()) {
            // Loop over Constrain Cell Pairs
            for(auto& pair_json : tile_json["constrain_cell_pairs"].array_items()) {
                ConstrainCellPair_t pair;
                pair.driver_bel = pair_json["driver_bel"].string_value();
                pair.driver_port = pair_json["driver_port"].string_value();
                pair.user_bel = pair_json["user_bel"].string_value();
                pair.user_port = pair_json["user_port"].string_value();
                constrain_cell_pairs.push_back(pair);
            }
        }
        log_info("pcbFPGA: Parsed %ld constrain cell pairs\n", constrain_cell_pairs.size());
    }
    // Create Tiles
    void init_tiles()
    {
        // Mesh must be at least 5x5 -> IOBs on each side and one CLB in the middle
        NPNR_ASSERT(X >= 5);
        NPNR_ASSERT(Y >= 5);

        // Parse Constrain Cell Pairs
        parse_constrain_cell_pairs();

        json11::Json iob_tile = find_tile_json("IOB");
        json11::Json clb_tile = find_tile_json("CLB");
        json11::Json icb_tile = find_tile_json("icb");
        json11::Json ccb_tile = find_tile_json("ccb");
        json11::Json qsb_tile = find_tile_json("qsb");
        json11::Json tsb_tile = find_tile_json("tsb");
        json11::Json dsb_tile = find_tile_json("dsb");

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
                    row_tile_types.at(x) = create_tile_from_json(x, y, qsb_tile);
                    continue;
                }
                if(mesh_utils::is_tsb(x, y, X, Y)) {
                    NPNR_ASSERT(row_tile_types.at(x) == "");
                    tsb_count++;
                    row_tile_types.at(x) = create_tile_from_json(x, y, tsb_tile);
                    continue;
                }
                if(mesh_utils::is_dsb(x, y, X, Y)) {
                    NPNR_ASSERT(row_tile_types.at(x) == "");
                    dsb_count++;
                    row_tile_types.at(x) = create_tile_from_json(x, y, dsb_tile);
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

    void create_bel_inout_wire(const int x, const int y, const std::string& inout_name, const std::string& bel_name, const BelId b, TileWireMap_t& tile_wires, const PortType io_type) {
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

    void add_cell_inout_timing(const IdString cell_id, const std::string& inout_name, const json11::Json timing, bool debug = false) {
        // Construct inout name id 
        int inout_start, inout_end;
        IdString inout_id = ctx->id(parse_name_range(inout_name, inout_end, inout_start));

        // Check if inout is a range
        if(inout_start != inout_end) {
            log_error("pcbFPGA: add_cell_inout_timing: Ranges not supported yet\n");
        }

        for(const auto& timing : timing.array_items()) {
            if(!timing.is_object()) {
                log_error("pcbFPGA: add_cell_inout_timing: Timing not an object for inout named %s\n", inout_name.c_str());
                continue;
            }

            if(timing["is_clock"].bool_value()) {
                if(debug)
                    log_info("pcbFPGA: \t\t\tAdding clock to %s\n", inout_name.c_str());
                ctx->addCellTimingClock(cell_id, inout_id);
                continue;
            }

            const std::string timing_type = timing["type"].string_value();
            if (timing_type == "delay") {
                int from_start, from_end;
                std::string from_name = parse_name_range(timing["from"].string_value(), from_end, from_start);
                delay_t value = std::max((float) lookup_param(timing["value"]).number_value(), 0.01f);

                for(int i = from_start; i <= from_end; i++) {
                    IdString from_id;
                    if(from_start == from_end) {
                        from_id = ctx->id(from_name);
                    } else {
                        from_id = ctx->id(from_name + "[" + std::to_string(i) + "]");
                    }

                    if(debug)
                        log_info("pcbFPGA: \t\t\tAdding delay from %s to %s with delay %f\n", from_name.c_str(), inout_name.c_str(), value);
                    ctx->addCellTimingDelay(cell_id, from_id, inout_id, value);
                }
            } else if (timing_type =="setup_hold") {
                delay_t setup = lookup_param(timing["setup"]).number_value();
                delay_t hold = lookup_param(timing["hold"]).number_value();
                if(debug)
                    log_info("pcbFPGA: \t\t\tAdding timing to %s -> %s with setup %f and hold %f\n", timing["clock"].string_value().c_str(), inout_name.c_str(), setup, hold);
                ctx->addCellTimingSetupHold(cell_id, inout_id, ctx->id(timing["clock"].string_value()), setup, hold);
            } else if (timing_type == "clock_to_out") {
                delay_t value = std::max((float) lookup_param(timing["value"]).number_value(), 0.01f);
                if(debug)
                    log_info("pcbFPGA: \t\t\tAdding clock to out timing to %s -> %s with delay %f\n", timing["clock"].string_value().c_str(), inout_name.c_str(), value);
                ctx->addCellTimingClockToOut(cell_id, inout_id, ctx->id(timing["clock"].string_value()), value);
            } else {
                log_error("pcbFPGA: add_bel_inout_timing: Timing type %s not implemented for inout named %s\n", timing_type.c_str(), inout_name.c_str());
            }
        }
    }

    // Add Cell timing
    void add_cell_timing(bool debug = false) {
        log_info("pcbFPGA: Adding cell timing...\n");

        int count = 0;
        for(const auto& cell : ctx->cells) {
            const auto* ci = cell.second.get();

            if(debug)
                log_info("Cell: %s CI Type: %s CI Name: %s\n", cell.first.c_str(ctx), ci->type.c_str(ctx), ci->name.c_str(ctx));

            if(bel_type_id_to_json.find(ci->type) == bel_type_id_to_json.end())
                log_error("pcbFPGA: add_cell_timing: No JSON configuration for cell type %s\n", ci->type.c_str(ctx));

            const auto& cell_json = bel_type_id_to_json[ci->type];

            // Add Timing
            for(const auto& input : cell_json["inputs"].array_items()) {
                if(!input.is_object())
                    continue;
                add_cell_inout_timing(ci->name, input["name"].string_value(), input["timing"], debug);
                count++;
            }
            for(const auto& inout : cell_json["inouts"].array_items()) {
                if(!inout.is_object())
                    continue;
                add_cell_inout_timing(ci->name, inout["name"].string_value(), inout["timing"], debug);
                count++;
            }
            for(const auto& output : cell_json["outputs"].array_items()) {
                if(!output.is_object())
                    continue;
                add_cell_inout_timing(ci->name, output["name"].string_value(), output["timing"], debug);
                count++;
            }
        }
        log_info("pcbFPGA: Annotated %ld cells with %d timings\n", ctx->cells.size(), count);
    }

    // Create Tile (BEL and Wires) from JSON configuration
    std::string create_tile_from_json(int x, int y, json11::Json tile_json) {
        log_info("pcbFPGA: Creating tile at (%d, %d)\n", x, y);

        // Create BELs
        
        // How many times to repeat the bels
        const int repetitions = std::max(lookup_param(tile_json["bel_repetitions"]).int_value(), 1);
        const int bels_per_repetition = tile_json["bels"].array_items().size();
        for(int rep = 0; rep < repetitions; rep++) {
            int bel_count = 0;
            for (const auto &bel_json : tile_json["bels"].array_items()) {
                // Get config
                const int num_per_tile = std::max(lookup_param(bel_json["num_per_tile"]).int_value(), 1);
                const std::string bel_name = bel_json["name"].string_value();

                // Get the tile wires
                auto& tile_wires = wires_per_tile.at(y).at(x);

                log_info("pcbFPGA: \tCreating %d %s BELs at (%d, %d)\n", num_per_tile, bel_name.c_str(), x, y);

                // ID of BEL type -> Same for all BELs of this type
                IdString bel_type_id = ctx->idf("%s", bel_name.c_str());

                // Create num_per_tile BELs
                for (int z = bel_count + rep * bels_per_repetition; z < bel_count + num_per_tile + rep * bels_per_repetition; z++) {
                    // Unique BEL ID at this location
                    std::string unique_bel_name = bel_name + std::to_string(z);
                    IdString unique_bel_id = ctx->id(unique_bel_name);

                    log_info("pcbFPGA: \t\tCreating BEL %s%d at (%d, %d, %d)\n", bel_name.c_str(), z, x, y, z);

                    // Create BEL
                    BelId b = ctx->addBel(h.xy_id(x, y, unique_bel_id), bel_type_id, Loc(x, y, z), false, false);

                    // Add Inputs
                    if (bel_json["inputs"].is_array()) {
                        for (const auto &input : bel_json["inputs"].array_items()) {
                        std::string name;
                        if(input.is_string()) {
                            name = input.string_value();
                        } else {
                            name = input["name"].string_value();
                        }
                        create_bel_inout_wire(x, y, name, unique_bel_name, b, tile_wires, PORT_IN);
                        }
                    }

                    // Add Inouts
                    if (bel_json["inouts"].is_array()) {
                        for (const auto &inout : bel_json["inouts"].array_items()) {
                        std::string name;
                        if(inout.is_string()) {
                            name = inout.string_value();
                        } else {
                            name = inout["name"].string_value();
                        }
                            create_bel_inout_wire(x, y, name, unique_bel_name, b, tile_wires, PORT_INOUT);
                        }
                    }

                    // Add Outputs
                    if (bel_json["outputs"].is_array()) {
                        for (const auto &output : bel_json["outputs"].array_items()) {
                        std::string name;
                        if(output.is_string()) {
                            name = output.string_value();
                        } else {
                            name = output["name"].string_value();
                        }
                            create_bel_inout_wire(x, y, name, unique_bel_name, b, tile_wires, PORT_OUT);
                        }
                    }
                }
                bel_count += num_per_tile;
                if(bel_type_id_to_json.find(bel_type_id) == bel_type_id_to_json.end())
                    bel_type_id_to_json[bel_type_id] = bel_json;
            }
        }

        // Create Wires
        for(const auto& wire_json : tile_json["wires"].array_items()) {
            // Get config
            const std::string wire_name = wire_json["name"].string_value();
            const int num_wires = std::max(lookup_param(wire_json["num_wires"]).int_value(), 1);

            // Get the tile wires
            auto& tile_wires = wires_per_tile.at(y).at(x);

            log_info("pcbFPGA: \tCreating %d %s wires at (%d, %d)\n", num_wires, wire_name.c_str(), x, y);

            // Create num_per_tile wires
            if(num_wires > 1) {
                for (int z = 0; z < num_wires; z++) {
                    // Unique wire ID at this location
                    std::string unique_wire_name = wire_name + "[" + std::to_string(z) + "]";
                    IdString unique_wire_id = ctx->id(unique_wire_name);

                    log_info("pcbFPGA: \t\tCreating wire %s%d at (%d, %d, %d)\n", wire_name.c_str(), z, x, y, z);

                    // Create Wire
                    WireId w = ctx->addWire(h.xy_id(x, y, unique_wire_id), unique_wire_id, x, y);
                    tile_wires["TILE"].push_back(w);
                }
            } else {
                // Unique wire ID at this location
                IdString unique_wire_id = ctx->id(wire_name);

                log_info("pcbFPGA: \t\tCreating wire %s at (%d, %d)\n", wire_name.c_str(), x, y);

                // Create Wire
                WireId w = ctx->addWire(h.xy_id(x, y, unique_wire_id), unique_wire_id, x, y);
                tile_wires["TILE"].push_back(w);
            }
        }

        return tile_json["tile_type"].string_value();
    }

    // Create PIPs
    void init_pips(bool debug = false) {
        log_info("pcbFPGA: Creating PIPs...\n");
        dict<std::string, int> pips_per_tile_type;
        for(int y = 0; y < Y; y++) {
            auto& row_tile_types = tile_types.at(y);
            for(int x = 0; x < X; x++) {
                auto tile_json = find_tile_json(row_tile_types.at(x), false);
                if (tile_json != json11::Json()) {
                    std::string tile_type_str = tile_json["tile_type"].string_value();
                    log_info("pcbFPGA: Creating PIPs for tile %s at (%d, %d)\n", tile_type_str.c_str(), x, y);
                    int count = 0;
                    for (const auto& pip_json : tile_json["pips"].array_items()) {
                        count += create_pips_for_tile(x, y, pip_json, false, debug);
                    }
                    for (const auto& pip_json : tile_json["internal_pips"].array_items()) {
                        count += create_pips_for_tile(x, y, pip_json, true, debug);
                    }
                    if(pips_per_tile_type.find(tile_type_str) == pips_per_tile_type.end()) {
                        pips_per_tile_type[tile_type_str] = count;
                    } else {
                        pips_per_tile_type[tile_type_str] += count;
                    }
                }
            }
        }
        for(const auto& pip : pips_per_tile_type) {
            log_info("pcbFPGA: Created %d PIPs for tile type %s\n", pip.second, pip.first.c_str());
        }
    }

    typedef enum {
        FULL,
        WILTON,
        DISJOINT
    } connection_style_e;

    // Connection Style to String
    std::string connection_style_to_string(const connection_style_e connection) const {
        switch(connection) {
            case FULL:
                return "full";
            case WILTON:
                return "wilton";
            case DISJOINT:
                return "disjoint";
        }
        return "unknown";
    }

    // Parse Connection Style
    connection_style_e parse_connection_style(const std::string& connection) const {
        if(connection == "" || connection == "full") {
            return FULL;
        } else if(connection == "wilton") {
            return WILTON;
        } else if(connection == "disjoint") {
            return DISJOINT;
        } else {
            log_error("pcbFPGA: Connection style %s not supported\n", connection.c_str());
        }
    }

    // Create PIPs for a tile
    int create_pips_for_tile(int x, int y, json11::Json pip_json, bool internal, bool debug = false) {
        // Connection style -> Default is full
        const connection_style_e connection_style = parse_connection_style(lookup_param(pip_json["connection"]).string_value());

        // Get Tile names
        const std::string src_tile_name = pip_json["src_tile"].string_value();
        const std::string dst_tile_name = pip_json["dst_tile"].string_value();

        // Get Wire names
        const std::string src_wire_name = pip_json["src_wire"].string_value();
        const std::string dst_wire_name = pip_json["dst_wire"].string_value();

        // Get Delay
        const auto delay_json = lookup_param(pip_json["delay"]);
        if(!delay_json.is_null() && !delay_json.is_number()) {
            log_error("pcbFPGA: PIP delay not a floatingpoint value!\n");
        }
        const delay_t delay = std::max((float) lookup_param(delay_json).number_value(), 0.01f);

        // Find the source and destination tiles and bel wires
        auto src_wires_per_bel = find_wires_for_tile_neighbour(x, y, src_tile_name, lookup_param(pip_json["src_loc"]).string_value(), debug);
        auto dst_wires_per_bel = find_wires_for_tile_neighbour(x, y, dst_tile_name, lookup_param(pip_json["dst_loc"]).string_value(), debug);

        // Filter out the source wires
        auto src_wires = filter_bel_wires(src_wires_per_bel, src_wire_name);
        auto dst_wires = filter_bel_wires(dst_wires_per_bel, dst_wire_name);

        if(debug)
            log_info("%ld %ld %ld %ld\n", src_wires_per_bel.size(), dst_wires_per_bel.size(), src_wires.size(), dst_wires.size());

        int count = 0;
        for(const auto& src_wire : src_wires) {
            std::string hier_src_wire_name = ctx->nameOfWire(src_wire);
            std::string src_x_y = hier_src_wire_name.substr(0, hier_src_wire_name.find_last_of("/"));
            std::string src_wire_name_and_index = hier_src_wire_name.substr(hier_src_wire_name.find_last_of("/") + 1);
            std::string src_wire_index = src_wire_name_and_index.substr(src_wire_name_and_index.find_last_of("[") + 1, src_wire_name_and_index.find_last_of(']'));
            
            for(const auto& dst_wire : dst_wires) {
                std::string hier_dst_wire_name = ctx->nameOfWire(dst_wire);
                std::string dst_x_y = hier_dst_wire_name.substr(0, hier_dst_wire_name.find_last_of("/"));
                std::string dst_wire_name_and_index = hier_dst_wire_name.substr(hier_dst_wire_name.find_last_of("/") + 1);
                std::string dst_wire_index = dst_wire_name_and_index.substr(dst_wire_name_and_index.find_last_of("[") + 1, dst_wire_name_and_index.find_last_of(']'));

                if (!internal && (src_x_y == dst_x_y)) {
                    if(debug)
                        log_info("pcbFPGA: \tSkipping PIP from %s to %s with delay %f\n", ctx->nameOfWire(src_wire), ctx->nameOfWire(dst_wire), delay);
                    continue;
                }
                if(debug)
                    log_info("pcbFPGA: \tCreating PIP from %s to %s with delay %f\n", ctx->nameOfWire(src_wire), ctx->nameOfWire(dst_wire), delay);

                // Disjoint connection -> Only same wires with same index can be connected
                if((connection_style == DISJOINT) && (src_wire_index != dst_wire_index)) {
                    if(debug)
                        log_info("pcbFPGA: \t\tSkipping PIP due to disjoint connection style %s != %s\n", src_wire_index.c_str(), dst_wire_index.c_str());
                    continue;
                }
                add_pip(Loc(x, y, 0), src_wire, dst_wire, delay);
                count++;
            }
        }
        return count;
    }

    std::vector<WireId> filter_bel_wires(const TileWireMap_t& bel_wires, const std::string& wire_name, bool debug = false) {
        std::vector<WireId> wires;
        for(const auto& bel_wire : bel_wires) {
            for(const auto& wire : bel_wire.second) {
                // Get Hierarchical Name
                std::string hier_wire_name = ctx->nameOfWire(wire);
                // Get the wire name
                std::string bel_wire_name = hier_wire_name.substr(hier_wire_name.find_last_of("/") + 1);

                if(debug)
                    log_info("pcbFPGA: \t\tChecking wire %s %s\n", hier_wire_name.c_str(), bel_wire_name.c_str());

                // Remove Bel from wire name
                if(bel_wire_name.find_last_of("_") != std::string::npos) {
                    bel_wire_name = bel_wire_name.substr(bel_wire_name.find_last_of("_") + 1);
                    if(debug)
                        log_info("removed _ %s\n", bel_wire_name.c_str());
                }

                if(wire_name.find("[") == std::string::npos) {
                    // No range in wire name to search for -> remove from bel wire name
                    bel_wire_name = bel_wire_name.substr(0, bel_wire_name.find_first_of("["));
                    if(debug)
                        log_info("removed [] %s\n", bel_wire_name.c_str());
                }
                // Check if wire name matches
                if(debug)
                    log_info("%s ?= %s\n", bel_wire_name.c_str(), wire_name.c_str());
                if(bel_wire_name == wire_name) {
                    wires.push_back(wire);
                    if(debug)
                        log_info("pcbFPGA: \t\tFound wire %s\n", hier_wire_name.c_str());
                }
            }
        }
        return wires;
    }

    // Find BELs for a tile
    TileWireMap_t find_wires_for_tile_neighbour(int x, int y, const std::string& tile_name, const std::string& loc = "", bool debug = false) {
        TileWireMap_t wires;
        // Check tile itself
        if(loc == "" || loc == "self") {
            if (tile_types.at(y).at(x) == tile_name) {
                if(debug)
                    log_info("pcbFPGA: Found wires for same tile %s at (%d, %d)\n", tile_name.c_str(), x, y);
                for (auto& wire_dict_pair : wires_per_tile.at(y).at(x)) {
                    for (const auto& wire : wire_dict_pair.second) {
                        wires[wire_dict_pair.first].push_back(wire);
                    }
                }
            }
        }
        // Check top neighbour
        if(loc == "" || loc == "north") {
            if ((y > 0) && (tile_types.at(y - 1).at(x) == tile_name)) {
                if(debug)
                    log_info("pcbFPGA: Found wires for tile top neighbor %s at (%d, %d)\n", tile_name.c_str(), x, y - 1);
                for (auto& wire_dict_pair : wires_per_tile.at(y - 1).at(x)) {
                    for (const auto& wire : wire_dict_pair.second) {
                        wires[wire_dict_pair.first].push_back(wire);
                    }
                }
            }
        }
        // Check bottom neighbour
        if(loc == "" || loc == "south") {
            if ((y < (Y - 1)) && (tile_types.at(y + 1).at(x) == tile_name)) {
                if(debug)
                    log_info("pcbFPGA: Found wires for tile bottom neighbor %s at (%d, %d)\n", tile_name.c_str(), x, y + 1);
                for (auto& wire_dict_pair : wires_per_tile.at(y + 1).at(x)) {
                    for (const auto& wire : wire_dict_pair.second) {
                        wires[wire_dict_pair.first].push_back(wire);
                    }
                }
            }
        }
        // Check left neighbour
        if(loc == "" || loc == "west") {
            if ((x > 0) && (tile_types.at(y).at(x - 1) == tile_name)) {
                if(debug)
                    log_info("pcbFPGA: Found wires for tile left neighbor %s at (%d, %d)\n", tile_name.c_str(), x - 1, y);
                for (auto& wire_dict_pair : wires_per_tile.at(y).at(x - 1)) {
                    for (const auto& wire : wire_dict_pair.second) {
                        wires[wire_dict_pair.first].push_back(wire);
                    }
                }
            }
        }
        // Check right neighbour
        if(loc == "" || loc == "east") {
            if ((x < (X - 1)) && (tile_types.at(y).at(x + 1) == tile_name)) {
                if(debug)
                    log_info("pcbFPGA: Found wires for tile right neighbor %s at (%d, %d)\n", tile_name.c_str(), x + 1, y);
                for (auto& wire_dict_pair : wires_per_tile.at(y).at(x + 1)) {
                    for (const auto& wire : wire_dict_pair.second) {
                        wires[wire_dict_pair.first].push_back(wire);
                    }
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
