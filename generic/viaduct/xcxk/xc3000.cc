// Tobias Senti 2024 <git@tsenti.li>

#include "xc3000.h"

#define VIADUCT_CONSTIDS "viaduct/xcxk/constids.inc"
#include "viaduct_constids.h"

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

    xc3000::init_decal_graphics();
    xc3000::build_tiles();

    log_info("Initialized device XC%s with %ld rows and %ld cols\n", device.c_str(), this->rows, this->cols);
}

char idx_to_letter(size_t idx) {
    assert(idx < 26);
    return (char)('A' + idx);
}

void xc3000::init_decal_graphics() {
    if(!this->with_gui)
        return;

    // Build CLB decals
    const float clb_x = this->tile_decal_size / 4 * 3 - this->clb_decal_width  / 2;
    const float clb_y = this->tile_decal_size / 4 * 1 - this->clb_decal_height / 2;

    ctx->addDecalGraphic(IdStringList(id_CLB),
        GraphicElement(GraphicElement::TYPE_BOX, GraphicElement::STYLE_INACTIVE, clb_x, clb_y, clb_x + this->clb_decal_width, clb_y + this->clb_decal_height, 10.0));

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
}

void xc3000::build_clb_at(size_t x, size_t y) {
    const char row_letter = idx_to_letter(y - 1);
    const char col_letter = idx_to_letter(x - 1);

    //log_info("Building CLB at (%2ld, %2ld): %c%c\n", x, y, row_letter, col_letter);

    auto lut_bel = ctx->addBel(h->xy_id(x, y, ctx->idf("%c%c_LUT", row_letter, col_letter)), id_LUT, Loc(x, y, 0), false, false);

    auto dff1_bel = ctx->addBel(h->xy_id(x, y, ctx->idf("%c%c_DFF1", row_letter, col_letter)), id_DFF, Loc(x, y, 1), false, false);
    auto dff2_bel = ctx->addBel(h->xy_id(x, y, ctx->idf("%c%c_DFF2", row_letter, col_letter)), id_DFF, Loc(x, y, 2), false, false);

    auto clb_group = GroupId(ctx->idf("%c%c", row_letter, col_letter));

    ctx->addGroupBel(clb_group, lut_bel);
    ctx->addGroupBel(clb_group, dff1_bel);
    ctx->addGroupBel(clb_group, dff2_bel);

    if(this->with_gui) {
        const float clb_y = this->rows - y + 1;
        ctx->setGroupDecal(clb_group, x, clb_y, IdStringList(id_CLB));
        ctx->setBelDecal(lut_bel,  x, clb_y, IdStringList(id_LUT));
        ctx->setBelDecal(dff1_bel, x, clb_y, IdStringList(id_DFF_QX));
        ctx->setBelDecal(dff2_bel, x, clb_y, IdStringList(id_DFF_QY));
    }
}

void xc3000::build_iob_at(size_t x, size_t y) {
    size_t pad_num = 0;
    if(y == 0) {
        // Top
        pad_num = x;
    } else if(x == this->cols + 1) {
        // Right
        pad_num = this->cols + y;
    } else if(y == this->rows + 1) {
        // Bottom
        pad_num = this->rows + 2 * this->cols - x;
    } else if(x == 0) {
        // Left
        pad_num = 2 * (this->rows + this->cols) - y;
    }
    log_info("Building IOB at (%2ld, %2ld): PAD%ld\n", x, y, pad_num);

    auto iob1 = ctx->addBel(h->xy_id(x, y, ctx->idf("PAD%ld", pad_num * 2 - 1)), id_IOB, Loc(x, y, 0), false, false);
    auto iob2 = ctx->addBel(h->xy_id(x, y, ctx->idf("PAD%ld", pad_num * 2)), id_IOB, Loc(x, y, 1), false, false);

    if(this->with_gui) {
        const float iob_y = this->rows - y + 1;
        if(y == 0) {
            // Top
            ctx->setBelDecal(iob1, x, iob_y, IdStringList(id_IOB_TOP_BOTTOM_1));
            ctx->setBelDecal(iob2, x, iob_y, IdStringList(id_IOB_TOP_BOTTOM_2));
        } else if(x == this->cols + 1) {
            // Right
            ctx->setBelDecal(iob2, x, iob_y, IdStringList(id_IOB_LEFT_RIGHT_1));
            ctx->setBelDecal(iob1, x, iob_y, IdStringList(id_IOB_LEFT_RIGHT_2));
        } else if(y == this->rows + 1) {
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

void xc3000::build_tiles() {
    for(size_t y = 0; y <= this->rows+1; y++) {
        for(size_t x = 0; x <= this->cols+1; x++) {
            // Top/Bottom IOBs
            if((x == 0 || x == this->cols+1)) {
                if(y > 0 && y <= this->rows) {
                    build_iob_at(x, y);
                }
                continue;
            }
            // Left/Right IOBs
            if((y == 0 || y == this->rows+1)) {
                if(x > 0 && x <= this->cols) {
                    build_iob_at(x, y);
                }
                continue;
            }
            // CLBs
            build_clb_at(x, y);
        }
    }
}

NEXTPNR_NAMESPACE_END