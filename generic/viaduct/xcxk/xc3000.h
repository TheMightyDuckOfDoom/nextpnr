// Tobias Senti 2024 <git@tsenti.li>

#include "log.h"
#include "nextpnr.h"
#include "util.h"
#include "viaduct_api.h"
#include "viaduct_helpers.h"

NEXTPNR_NAMESPACE_BEGIN

#define LUT5_Z 0
#define LUT_F_Z LUT5_Z + 1
#define QX_Z LUT_F_Z + 1
#define LUT_G_Z QX_Z + 1
#define QY_Z LUT_G_Z + 1

class xc3000 {
public:
    void init_device(Context* ctx, ViaductHelpers* h, std::string device, bool with_gui);

private:
    std::map<std::string, std::vector<std::string>> magic_connections;
    std::map<std::string, std::vector<std::string>> clb_iob_local_long_pips;
    std::vector<std::vector<std::map<IdString, WireId>>> tile_wires;

    // Graphics settings
    const float tile_decal_size = 1.0f;
    const float clb_decal_width = 0.3f;
    const float clb_decal_height = 0.4f;
    const float lut_decal_width = 0.06f;
    const float lut_decal_height = 0.15f;
    const float dff_decal_size = 0.05f;
    const float iob_decal_size = 0.3f;
    const float magic_decal_size = 0.30f;

    // Device settings
    const size_t MAGIC_WIRES_PER_SIDE = 5;

    Context* ctx;                          
    ViaductHelpers* h;
    bool with_gui;

    size_t rows, cols;
    size_t num_iobs;

    void get_magic_decal_coord(size_t wire, float& x, float& y);
    void read_device(std::string device);
    void init_decal_graphics();
    void build_tiles();
    void build_magic_at(size_t x, size_t y);
    void build_clb_at(size_t x, size_t y);
    void build_iob_at(size_t x, size_t y);
    void build_tile_wires(size_t x, size_t y);
    void build_clb_wires(size_t x, size_t y);
};

NEXTPNR_NAMESPACE_END
