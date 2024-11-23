// Tobias Senti 2024 <git@tsenti.li>

#include "log.h"
#include "nextpnr.h"
#include "util.h"
#include "viaduct_api.h"
#include "viaduct_helpers.h"

NEXTPNR_NAMESPACE_BEGIN

class xc3000 {
public:
    void init_device(Context* ctx, ViaductHelpers* h, std::string device, bool with_gui);

private:
    // Graphics settings
    const float tile_decal_size = 1.0f;
    const float clb_decal_width = 0.3f;
    const float clb_decal_height = 0.4f;
    const float lut_decal_width = 0.075f;
    const float lut_decal_height = 0.2f;
    const float dff_decal_size = 0.05f;
    const float iob_decal_size = 0.3f;

    Context* ctx;                          
    ViaductHelpers* h;
    bool with_gui;

    size_t rows, cols;
    size_t num_iobs;

    void init_decal_graphics();
    void build_tiles();
    void build_clb_at(size_t x, size_t y);
    void build_iob_at(size_t x, size_t y);
};

NEXTPNR_NAMESPACE_END
