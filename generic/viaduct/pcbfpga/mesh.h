#include <vector>
#include "nextpnr.h"
#include "viaduct_helpers.h"

#ifndef MESH_H
#define MESH_H

NEXTPNR_NAMESPACE_BEGIN

typedef struct {
    size_t clbs_x;
    size_t clbs_y;
    size_t dimX;
    size_t dimY;
    size_t channel_width;
} mesh_config_t;

typedef enum {
    TILE_NONE = 0,
    TILE_IOB = 1,
    TILE_CLB = 2,
    TILE_QSB = 3,
    TILE_QCB = 4,
    TILE_COR = 5
} tile_type_t;

typedef std::vector<std::vector<tile_type_t>> mesh_t;

typedef std::map<std::string, std::vector<WireId>> wire_map_t;
typedef std::vector<std::vector<wire_map_t>> wire_mesh_t;

struct Mesh {
    // Config
    size_t DIM_X;
    size_t DIM_Y;
    size_t CLBS_X;
    size_t CLBS_Y;

    // Routing channel width
    const size_t CHANNEL_WIDTH = 16;

    // Number of inputs to a LUT
    const size_t LUT_INPUTS = 4;
    // LUT inputs + dedicated DFF D input
    const size_t SLICE_INPUTS = LUT_INPUTS + 1;
    // LUT F and DFF Q outputs
    const size_t SLICE_OUTPUTS = 2;
    // Number of slices per CLB
    const size_t SLICES_PER_CLB = 4;
    // One SLICE inputs + CLK
    const size_t CLB_INPUTS_PER_SIDE = SLICE_INPUTS + 1;
    // One SLICE outputs
    const size_t CLB_OUTPUTS_PER_SIDE = SLICE_OUTPUTS;
    // Number of IOBUFs per IOB tile
    const size_t IO_PER_IOB = 2;

    // If this is true, CLB inputs are only connect to every other channel
    const bool SPARSE_CLB_INPUT = true;
    // If this is true, CLB outputs are only connect to every other channel
    const bool SPARSE_CLB_OUTPUT = false;
    // Add a pip from the LUT F output to the DFF D input
    const bool LUT_F_TO_DFF_D = true;

    const double DUMMY_DELAY = 0.05;
    const double MUX2_DELAY = 2.3;
    const double MUX8_DELAY = 19.0;
    const double QCB_INPUT_DELAY = MUX8_DELAY;
    const double LUT_DELAY = MUX2_DELAY + MUX8_DELAY;
    const double DFF_SETUP = 1.5;
    const double DFF_HOLD = 0.5;
    const double DFF_CLK_TO_Q = 2.5;

    mesh_t mesh;
    wire_mesh_t wire_mesh;

    void init(Context *ctx, ViaductHelpers *h, size_t clbs_x, size_t clbs_y);
    void build();
    void update_timing();
private:
    Context *ctx;
    ViaductHelpers *h;

    void print();
    void build_mesh();
    void build_wires();
    void build_pips();
    void build_bels();

    wire_map_t build_qcb_wires(size_t x, size_t y);
    wire_map_t build_clb_wires(size_t x, size_t y);
    wire_map_t build_iob_wires(size_t x, size_t y);

    void build_corner_pips(size_t x, size_t y);
    void build_qsb_pips(size_t x, size_t y);
    void build_qcb_pips(size_t x, size_t y);
    void build_clb_pips(size_t x, size_t y);
    void build_iob_pips(size_t x, size_t y);

    void build_clb_bels(size_t x, size_t y);
    void build_iob_bels(size_t x, size_t y);

    void update_lut_timing(const CellInfo *ci);
    void update_dff_timing(const CellInfo *ci);
};

// Build a mesh of tiles
mesh_t mesh_build(mesh_config_t &cfg);
wire_mesh_t mesh_build_wires(Context *ctx, const mesh_t &mesh, const mesh_config_t &cfg);

// Print mesh
void mesh_print(const mesh_t &mesh);

NEXTPNR_NAMESPACE_END

#endif
