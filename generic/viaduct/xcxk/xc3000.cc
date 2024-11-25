// Tobias Senti 2024 <git@tsenti.li>

#include "xc3000.h"

#define VIADUCT_CONSTIDS "viaduct/xcxk/constids.inc"
#include "viaduct_constids.h"

#include "command.h"
#include <fstream>
#include <boost/filesystem/path.hpp>

NEXTPNR_NAMESPACE_BEGIN

void xc3000::init_device(Context* ctx, ViaductHelpers* h, std::string device, bool with_gui) {
    this->ctx = ctx;
    this->h = h;
    this->with_gui = with_gui;

    if(device == "3020" || device == "3020A" || device == "3120A" || device == "3020L") {
        this->rows = 8;
        this->cols = 8;
    } else if(device == "3090" || device == "3090A" || device == "3190A" || device == "3090L" || device == "3190L") {
        this->rows = 20;
        this->cols = 16;
    } else if(device == "3195A") {
        this->rows = 22;
        this->cols = 22;
    } else {
        log_error("Unknown device %s\n", device.c_str());
    }

    this->num_iobs = (2 * (this->rows + this->cols)) * 2;

    xc3000::read_device(device);
    xc3000::init_decal_graphics();
    xc3000::build_tiles();

    log_info("Initialized device XC%s with %ld rows and %ld cols\n", device.c_str(), this->rows, this->cols);
}

std::string get_path(std::string device, std::string file) {
    std::string path = proc_share_dirname();
    path += "generic/xcxk/";
    path += device;
    path += "/";
    path += file;
    boost::filesystem::path p(path);
    path = p.make_preferred().string();

    return path;
}

void xc3000::read_device(std::string device) {
    init_share_dirname();
    std::string path = get_path(device, "MAGIC_CONNECTIONS.txt");
    std::ifstream if_magic_connections(path);
    
    if(!if_magic_connections.is_open()) {
        log_error("Unable to open %s\n", path.c_str());
    }

    std::string line;
    while(getline(if_magic_connections, line)) {
        if(line.find("\n") != std::string::npos)
            line = line.replace(line.find_first_of("\n"), 1, "");
        if(line == "")
            continue;

        std::string magic_name = line.substr(0, line.find_first_of("."));
        printf("Magic name: %s\n", magic_name.c_str());

        if(this->magic_connections.find(magic_name) == this->magic_connections.end())
            this->magic_connections[magic_name] = std::vector<std::string>();
    
        this->magic_connections[magic_name].push_back(line);
    }
}

char idx_to_letter(size_t idx) {
    assert(idx < 26);
    return (char)('A' + idx);
}

void xc3000::get_magic_decal_coord(size_t wire, float& x, float& y) {
    x = 0;
    y = 0;

    if(wire < MAGIC_WIRES_PER_SIDE) {
        x = (wire + 1) * this->magic_decal_size / (MAGIC_WIRES_PER_SIDE + 1);
        y = this->magic_decal_size;
    } else if(wire < 2 * MAGIC_WIRES_PER_SIDE) {
        x = this->magic_decal_size;
        y = (2 * MAGIC_WIRES_PER_SIDE - wire) * this->magic_decal_size / (MAGIC_WIRES_PER_SIDE + 1);
    } else if(wire < 3 * MAGIC_WIRES_PER_SIDE) {
        x = (3 * MAGIC_WIRES_PER_SIDE - wire) * this->magic_decal_size / (MAGIC_WIRES_PER_SIDE + 1);
    } else {
        y = this->magic_decal_size - (4 * MAGIC_WIRES_PER_SIDE - wire) * this->magic_decal_size / (MAGIC_WIRES_PER_SIDE + 1);
    }
}

void xc3000::init_decal_graphics() {
    if(!this->with_gui)
        return;

    // Build CLB decals
    const float clb_x = this->tile_decal_size / 4 * 3 - this->clb_decal_width  / 2;
    const float clb_y = this->tile_decal_size / 4 * 1 - this->clb_decal_height / 2;

    ctx->addDecalGraphic(IdStringList(id_CLB),
        GraphicElement(GraphicElement::TYPE_BOX, GraphicElement::STYLE_FRAME, clb_x, clb_y, clb_x + this->clb_decal_width, clb_y + this->clb_decal_height, 10.0));

    // Build LUT decals
    const float lut_x = clb_x + (this->clb_decal_width  / 4) - this->lut_decal_width / 2;
    const float lut_y = clb_y + (this->clb_decal_height / 2) - this->lut_decal_height / 2;

    ctx->addDecalGraphic(IdStringList(id_LUT),
        GraphicElement(GraphicElement::TYPE_BOX, GraphicElement::STYLE_INACTIVE, lut_x, lut_y, lut_x + this->lut_decal_width, lut_y + this->lut_decal_height, 10.0));

    // Build DFF decals
    const float dff_x = clb_x + (this->clb_decal_width   / 4 * 3) - this->dff_decal_size / 2;
    const float dff_y = clb_y + (this->clb_decal_height  / 3 * 1) - this->dff_decal_size / 2;
    ctx->addDecalGraphic(IdStringList(id_DFF_QY),
        GraphicElement(GraphicElement::TYPE_BOX, GraphicElement::STYLE_INACTIVE, dff_x, dff_y, dff_x + this->dff_decal_size, dff_y + this->dff_decal_size, 10.0));
    ctx->addDecalGraphic(IdStringList(id_DFF_QX),
        GraphicElement(GraphicElement::TYPE_BOX, GraphicElement::STYLE_INACTIVE, dff_x, dff_y + this->clb_decal_height / 3, dff_x + this->dff_decal_size, dff_y + this->clb_decal_height / 3 + this->dff_decal_size, 10.0));

    // Build IOB decals
    const float iob_x = this->tile_decal_size / 2 - this->iob_decal_size / 2;
    const float iob_y = this->tile_decal_size / 4 - this->iob_decal_size / 2;
    ctx->addDecalGraphic(IdStringList(id_IOB_LEFT_RIGHT_1),
        GraphicElement(GraphicElement::TYPE_BOX, GraphicElement::STYLE_INACTIVE, iob_x, iob_y, iob_x + this->iob_decal_size, iob_y + this->iob_decal_size, 10.0));
    ctx->addDecalGraphic(IdStringList(id_IOB_LEFT_RIGHT_2),
        GraphicElement(GraphicElement::TYPE_BOX, GraphicElement::STYLE_INACTIVE, iob_x, iob_y + this->tile_decal_size / 2, iob_x + this->iob_decal_size, iob_y + this->tile_decal_size / 2 + this->iob_decal_size, 10.0));
    ctx->addDecalGraphic(IdStringList(id_IOB_TOP_BOTTOM_1),
        GraphicElement(GraphicElement::TYPE_BOX, GraphicElement::STYLE_INACTIVE, iob_y, iob_x, iob_y + this->iob_decal_size, iob_x + this->iob_decal_size, 10.0));
    ctx->addDecalGraphic(IdStringList(id_IOB_TOP_BOTTOM_2),
        GraphicElement(GraphicElement::TYPE_BOX, GraphicElement::STYLE_INACTIVE, iob_y + this->tile_decal_size / 2, iob_x, iob_y + this->tile_decal_size / 2 + this->iob_decal_size, iob_x + this->iob_decal_size, 10.0));

    // Build magic decals
    const float magic_x = this->tile_decal_size / 4 * 1 - this->magic_decal_size / 2;
    const float magic_y = this->tile_decal_size / 4 * 3 - this->magic_decal_size / 2;
    ctx->addDecalGraphic(IdStringList(id_MAGIC),
        GraphicElement(GraphicElement::TYPE_BOX, GraphicElement::STYLE_FRAME, magic_x, magic_y, magic_x + this->magic_decal_size, magic_y + this->magic_decal_size, 10.0));

    for(size_t magic_src = 0; magic_src < MAGIC_WIRES_PER_SIDE * 4; magic_src++) {
        for(size_t magic_dst = 0; magic_dst < MAGIC_WIRES_PER_SIDE * 4; magic_dst++) {
            if(magic_src == magic_dst)
                continue;

            float start_x, start_y, end_x, end_y;
            get_magic_decal_coord(magic_src, start_x, start_y);
            get_magic_decal_coord(magic_dst, end_x, end_y);
            start_x += magic_x;
            start_y += magic_y;
            end_x += magic_x;
            end_y += magic_y;

            ctx->addDecalGraphic(IdStringList(ctx->idf("MAGIC_PIP_%ld_%ld", magic_src, magic_dst)),
                GraphicElement(GraphicElement::TYPE_LOCAL_ARROW, GraphicElement::STYLE_INACTIVE, start_x, start_y, end_x, end_y, 10.0));
        }
    }

    // Local wires
    for(size_t w = 1; w <= MAGIC_WIRES_PER_SIDE; w++) {
        float row_start_x, row_start_y;
        get_magic_decal_coord(MAGIC_WIRES_PER_SIDE - 1 + w, row_start_x, row_start_y);
        row_start_x += magic_x;
        row_start_y += magic_y;
        float row_end_x, row_end_y;
        get_magic_decal_coord(4 * MAGIC_WIRES_PER_SIDE - w, row_end_x, row_end_y);
        row_end_x += magic_x + this->tile_decal_size;
        row_end_y += magic_y;
        // Normal Row
        ctx->addDecalGraphic(IdStringList(ctx->idf("row.local.%ld", w)),
            GraphicElement(GraphicElement::TYPE_LOCAL_LINE, GraphicElement::STYLE_INACTIVE, row_start_x, row_start_y, row_end_x, row_end_y, 10.0));

        // Row Left Top
        float tmp_x, tmp_y;
        get_magic_decal_coord(w - 1, tmp_x, tmp_y);
        ctx->addDecalGraphic(IdStringList(ctx->idf("row.local.%ld-topleft", w)),
            GraphicElement(GraphicElement::TYPE_LOCAL_LINE, GraphicElement::STYLE_INACTIVE, magic_x + tmp_x, row_start_y, row_end_x, row_end_y, 10.0));

        // Row Right Top
        get_magic_decal_coord(-w + MAGIC_WIRES_PER_SIDE, tmp_x, tmp_y);
        ctx->addDecalGraphic(IdStringList(ctx->idf("row.local.%ld-topright", w)),
            GraphicElement(GraphicElement::TYPE_LOCAL_LINE, GraphicElement::STYLE_INACTIVE, row_start_x, row_start_y, magic_x + this->tile_decal_size + tmp_x, row_end_y, 10.0));

        // Row Left Bottom
        get_magic_decal_coord(w + 2 * MAGIC_WIRES_PER_SIDE - 1, tmp_x, tmp_y);
        ctx->addDecalGraphic(IdStringList(ctx->idf("row.local.%ld-bottomleft", w)),
            GraphicElement(GraphicElement::TYPE_LOCAL_LINE, GraphicElement::STYLE_INACTIVE, magic_x + tmp_x, row_start_y, row_end_x, row_end_y, 10.0));

        // Row Right Bottom
        get_magic_decal_coord(w - 1, tmp_x, tmp_y);
        ctx->addDecalGraphic(IdStringList(ctx->idf("row.local.%ld-bottomright", w)),
            GraphicElement(GraphicElement::TYPE_LOCAL_LINE, GraphicElement::STYLE_INACTIVE, row_start_x, row_start_y, magic_x + this->tile_decal_size + tmp_x, row_end_y, 10.0));

        float col_start_x, col_start_y;
        get_magic_decal_coord(MAGIC_WIRES_PER_SIDE * 3 - w, col_start_x, col_start_y);
        col_start_x += magic_x;
        col_start_y += magic_y;
        float col_end_x, col_end_y;
        get_magic_decal_coord(-1 + w, col_end_x, col_end_y);
        col_end_x += magic_x;
        col_end_y += magic_y - this->tile_decal_size;
        // Normal Col
        ctx->addDecalGraphic(IdStringList(ctx->idf("col.local.%ld", w)),
            GraphicElement(GraphicElement::TYPE_LOCAL_LINE, GraphicElement::STYLE_INACTIVE, col_start_x, col_start_y, col_end_x, col_end_y, 10.0));

        // Col Left Top
        get_magic_decal_coord(-w + 4 * MAGIC_WIRES_PER_SIDE, tmp_x, tmp_y);
        ctx->addDecalGraphic(IdStringList(ctx->idf("col.local.%ld-topleft", w)),
            GraphicElement(GraphicElement::TYPE_LOCAL_LINE, GraphicElement::STYLE_INACTIVE, col_start_x, magic_y + tmp_y, col_end_x, col_end_y, 10.0));

        // Col Right Top
        get_magic_decal_coord(-w + 2 * MAGIC_WIRES_PER_SIDE, tmp_x, tmp_y);
        ctx->addDecalGraphic(IdStringList(ctx->idf("col.local.%ld-topright", w)),
            GraphicElement(GraphicElement::TYPE_LOCAL_LINE, GraphicElement::STYLE_INACTIVE, col_start_x, magic_y + tmp_y, col_end_x, col_end_y, 10.0));

        // Col Left Bottom
        get_magic_decal_coord(w + 3 * MAGIC_WIRES_PER_SIDE - 1, tmp_x, tmp_y);
        ctx->addDecalGraphic(IdStringList(ctx->idf("col.local.%ld-bottomleft", w)),
            GraphicElement(GraphicElement::TYPE_LOCAL_LINE, GraphicElement::STYLE_INACTIVE, col_start_x, col_start_y, col_end_x, magic_y + tmp_y - this->tile_decal_size, 10.0));

        // Col Right Bottom
        get_magic_decal_coord(w + MAGIC_WIRES_PER_SIDE - 1, tmp_x, tmp_y);
        ctx->addDecalGraphic(IdStringList(ctx->idf("col.local.%ld-bottomright", w)),
            GraphicElement(GraphicElement::TYPE_LOCAL_LINE, GraphicElement::STYLE_INACTIVE, col_start_x, col_start_y, col_end_x, magic_y + tmp_y - this->tile_decal_size, 10.0));
    }
}

void xc3000::build_magic_at(size_t x, size_t y) {
    const char row_letter = idx_to_letter(y - 1);
    const char col_letter = idx_to_letter(x - 1);

    auto magic_group = GroupId(ctx->idf("%c%c_MAGIC", row_letter, col_letter));

    std::string magic_name = std::string(1, row_letter) + std::string(1, col_letter);

    if(this->magic_connections.find(magic_name) == this->magic_connections.end()) {
        log_info("No magic connections found for magic %s\n", magic_name.c_str());
        return;
    }  

    for(std::string connection : magic_connections[magic_name]) {
        std::string src_port_name = connection.substr(connection.find_first_of(".20.1.") + 6, connection.find_first_of(" ") - connection.find_first_of(".20.1.") - 6); 
        std::string dst_port_name = connection.substr(connection.find_last_of(".") + 1);

        size_t src_magic_port = std::stoi(src_port_name);
        size_t dst_magic_port = std::stoi(dst_port_name);

        WireId src_wire;
        if(src_magic_port < MAGIC_WIRES_PER_SIDE) {
            src_wire = this->tile_wires[x][y-1][ctx->idf("col.%c.local.%ld", col_letter, src_magic_port + 1)];
        } else if(src_magic_port < MAGIC_WIRES_PER_SIDE * 2) {
            src_wire = this->tile_wires[x][y][ctx->idf("row.%c.local.%ld", row_letter, src_magic_port + 1 - MAGIC_WIRES_PER_SIDE)];
        } else if(src_magic_port < MAGIC_WIRES_PER_SIDE * 3) {
            src_wire = this->tile_wires[x][y][ctx->idf("col.%c.local.%ld", col_letter, -src_magic_port + 3 * MAGIC_WIRES_PER_SIDE)];
        } else {
            src_wire = this->tile_wires[x-1][y][ctx->idf("row.%c.local.%ld", row_letter, -src_magic_port + 4 * MAGIC_WIRES_PER_SIDE)];
        }
        
        WireId dst_wire;
        if(dst_magic_port < MAGIC_WIRES_PER_SIDE) {
            dst_wire = this->tile_wires[x][y-1][ctx->idf("col.%c.local.%ld", col_letter, dst_magic_port + 1)];
        } else if(dst_magic_port < MAGIC_WIRES_PER_SIDE * 2) {
            dst_wire = this->tile_wires[x][y][ctx->idf("row.%c.local.%ld", row_letter, dst_magic_port + 1 - MAGIC_WIRES_PER_SIDE)];
        } else if(dst_magic_port < MAGIC_WIRES_PER_SIDE * 3) {
            dst_wire = this->tile_wires[x][y][ctx->idf("col.%c.local.%ld", col_letter, -dst_magic_port + 3 * MAGIC_WIRES_PER_SIDE)];
        } else {
            dst_wire = this->tile_wires[x-1][y][ctx->idf("row.%c.local.%ld", row_letter, -dst_magic_port + 4 * MAGIC_WIRES_PER_SIDE)];
        }

        auto pip = ctx->addPip(IdStringList(ctx->id(connection)), id_MAGIC_PIP, src_wire, dst_wire, 0.1, Loc(x, y, 0));
        ctx->addGroupPip(magic_group, pip);

        if(this->with_gui) {
            const float pip_y = this->rows - y + 2;
            std::string decal_name = "MAGIC_PIP_" + src_port_name + "_" + dst_port_name;
            ctx->setPipDecal(pip, x, pip_y, IdStringList(ctx->id(decal_name)));
        }
    }

    if(this->with_gui) {
        const float magic_y = this->rows - y + 2;
        ctx->setGroupDecal(magic_group, x, magic_y, IdStringList(id_MAGIC));
    }
}

void xc3000::build_clb_at(size_t x, size_t y) {
    const char row_letter = idx_to_letter(y - 1);
    const char col_letter = idx_to_letter(x - 1);

    auto lut_bel = ctx->addBel(h->xy_id(x, y, ctx->idf("%c%c_LUT", row_letter, col_letter)), id_LUT, Loc(x, y, 0), false, false);

    auto dff_qx_bel = ctx->addBel(h->xy_id(x, y, ctx->idf("%c%c_QX", row_letter, col_letter)), id_DFF, Loc(x, y, 1), false, false);
    auto dff_qy_bel = ctx->addBel(h->xy_id(x, y, ctx->idf("%c%c_QY", row_letter, col_letter)), id_DFF, Loc(x, y, 2), false, false);

    auto clb_group = GroupId(ctx->idf("%c%c_CLB", row_letter, col_letter));

    ctx->addGroupBel(clb_group, lut_bel);
    ctx->addGroupBel(clb_group, dff_qx_bel);
    ctx->addGroupBel(clb_group, dff_qy_bel);

    if(this->with_gui) {
        const float clb_y = this->rows - y + 2;
        ctx->setGroupDecal(clb_group, x, clb_y, IdStringList(id_CLB));
        ctx->setBelDecal(lut_bel,  x, clb_y, IdStringList(id_LUT));
        ctx->setBelDecal(dff_qx_bel, x, clb_y, IdStringList(id_DFF_QX));
        ctx->setBelDecal(dff_qy_bel, x, clb_y, IdStringList(id_DFF_QY));
    }
}

void xc3000::build_iob_at(size_t x, size_t y) {
    size_t pad_num = 0;
    if(y == 0) {
        // Top
        pad_num = x;
    } else if(x == this->cols + 2) {
        // Right
        pad_num = this->cols + y;
    } else if(y == this->rows + 2) {
        // Bottom
        pad_num = this->rows + 2 * this->cols - x;
    } else if(x == 0) {
        // Left
        pad_num = 2 * (this->rows + this->cols) - y;
    }

    auto iob1 = ctx->addBel(h->xy_id(x, y, ctx->idf("PAD%ld", pad_num * 2 - 1)), id_IOB, Loc(x, y, 0), false, false);
    auto iob2 = ctx->addBel(h->xy_id(x, y, ctx->idf("PAD%ld", pad_num * 2)), id_IOB, Loc(x, y, 1), false, false);

    if(this->with_gui) {
        const float iob_y = this->rows - y + 2;
        if(y == 0) {
            // Top
            ctx->setBelDecal(iob1, x, iob_y, IdStringList(id_IOB_TOP_BOTTOM_1));
            ctx->setBelDecal(iob2, x, iob_y, IdStringList(id_IOB_TOP_BOTTOM_2));
        } else if(x == this->cols + 2) {
            // Right
            ctx->setBelDecal(iob2, x, iob_y, IdStringList(id_IOB_LEFT_RIGHT_1));
            ctx->setBelDecal(iob1, x, iob_y, IdStringList(id_IOB_LEFT_RIGHT_2));
        } else if(y == this->rows + 2) {
            // Bottom
            ctx->setBelDecal(iob2, x, iob_y, IdStringList(id_IOB_TOP_BOTTOM_1));
            ctx->setBelDecal(iob1, x, iob_y, IdStringList(id_IOB_TOP_BOTTOM_2));
        } else if(x == 0) {
            // Left
            ctx->setBelDecal(iob1, x, iob_y, IdStringList(id_IOB_LEFT_RIGHT_1));
            ctx->setBelDecal(iob2, x, iob_y, IdStringList(id_IOB_LEFT_RIGHT_2));
        }
    }
}

void xc3000::build_tile_wires(size_t x, size_t y) {
    const char row_letter = idx_to_letter(y - 1);
    const char col_letter = idx_to_letter(x - 1);

    // Local Wires
    for(size_t i = 1; i <= MAGIC_WIRES_PER_SIDE; i++) {
        if(x < this->rows + 1) {
            auto wire_id = ctx->idf("row.%c.local.%ld", row_letter, i);
            auto row_wire = ctx->addWire(h->xy_id(x, y, wire_id), id_LOCAL, x, y);
            this->tile_wires[x][y][wire_id] = row_wire;
            if(this->with_gui) {
                const float local_y = this->rows - y + 2;
                if(x == 1 && y == 1)
                    ctx->setWireDecal(row_wire, x, local_y, IdStringList(ctx->idf("row.local.%ld-topleft", i)));
                else if(x == this->rows && y == 1)
                    ctx->setWireDecal(row_wire, x, local_y, IdStringList(ctx->idf("row.local.%ld-topright", i)));
                else if(x == 1 && y == this->rows+1)
                    ctx->setWireDecal(row_wire, x, local_y, IdStringList(ctx->idf("row.local.%ld-bottomleft", i)));
                else if(x == this->rows && y == this->rows+1)
                    ctx->setWireDecal(row_wire, x, local_y, IdStringList(ctx->idf("row.local.%ld-bottomright", i)));
                else
                    ctx->setWireDecal(row_wire, x, local_y, IdStringList(ctx->idf("row.local.%ld", i)));
            }
        }
        if(y < this->cols + 1) {
            auto wire_id = ctx->idf("col.%c.local.%ld", col_letter, i);
            auto col_wire = ctx->addWire(h->xy_id(x, y, wire_id), id_LOCAL, x, y);
            this->tile_wires[x][y][wire_id] = col_wire;
            if(this->with_gui) {
                const float local_y = this->rows - y + 2;
                if(x == 1 && y == 1)
                    ctx->setWireDecal(col_wire, x, local_y, IdStringList(ctx->idf("col.local.%ld-topleft", i)));
                else if(x == this->rows+1 && y == 1)
                    ctx->setWireDecal(col_wire, x, local_y, IdStringList(ctx->idf("col.local.%ld-topright", i)));
                else if(x == 1 && y == this->cols)
                    ctx->setWireDecal(col_wire, x, local_y, IdStringList(ctx->idf("col.local.%ld-bottomleft", i)));
                else if(x == this->rows+1 && y == this->cols)
                    ctx->setWireDecal(col_wire, x, local_y, IdStringList(ctx->idf("col.local.%ld-bottomright", i)));
                else
                    ctx->setWireDecal(col_wire, x, local_y, IdStringList(ctx->idf("col.local.%ld", i)));
            }
        }
    }
}

void xc3000::build_tiles() {
    // Build mesh
    enum TileType {
        TILE_CLB,
        TILE_MAGIC_ONLY,
        IOB,
        NONE
    };
    std::vector<std::vector<TileType>> mesh(this->cols+3, std::vector<TileType>(this->rows+3, NONE));
    for(size_t y = 0; y <= this->rows+2; y++) {
        for(size_t x = 0; x <= this->cols+2; x++) {
            // Top/Bottom IOBs
            if((x == 0 || x == this->cols+2)) {
                if(y > 0 && y <= this->rows) {
                    mesh[x][y] = IOB;
                }
                continue;
            }
            // Left/Right IOBs
            if((y == 0 || y == this->rows+2)) {
                if(x > 0 && x <= this->cols) {
                    mesh[x][y] = IOB;
                }
                continue;
            }
            // CLBs
            if(x < this->cols+1 && y < this->rows+1) {
                mesh[x][y] = TILE_CLB;
            }
            // Magic
            if(mesh[x][y] == NONE)
                mesh[x][y] = TILE_MAGIC_ONLY;
        }
    }

    // Build wires
    this->tile_wires = std::vector<std::vector<std::map<IdString, WireId>>>(this->cols+3, std::vector<std::map<IdString, WireId>>(this->rows+3));
    for(size_t y = 0; y <= this->rows+2; y++) {
        for(size_t x = 0; x <= this->cols+2; x++) {
            if(mesh[x][y] == TILE_CLB || mesh[x][y] == TILE_MAGIC_ONLY) {
                build_tile_wires(x, y);
            }
        }
    }

    // Build Bels
    for(size_t y = 0; y <= this->rows+2; y++) {
        for(size_t x = 0; x <= this->cols+2; x++) {
            if(mesh[x][y] == TILE_CLB) {
                build_clb_at(x, y);
                build_magic_at(x, y);
            }
            else if(mesh[x][y] == TILE_MAGIC_ONLY) {
                build_magic_at(x, y);
            }
            else if(mesh[x][y] == IOB) {
                build_iob_at(x, y);
            }
        }
    }
}

NEXTPNR_NAMESPACE_END