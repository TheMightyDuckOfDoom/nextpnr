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
    size_t dimX;
    size_t dimY;
    size_t clbs_x;
    size_t clbs_y;
    size_t channel_width;
    const size_t lut_inputs = 4;
    const size_t slice_inputs = lut_inputs + 1;
    const size_t slice_outputs = 2;
    const size_t slices_per_clb = 4;
    const size_t clb_inputs_per_side = lut_inputs + 2;
    const size_t clb_outputs_per_side = 2;
    const size_t io_per_iob = 2;

    mesh_t mesh;
    wire_mesh_t wire_mesh;

    Mesh(Context *ctx, ViaductHelpers *h, size_t clbs_x, size_t clbs_y, size_t channel_width);
    void build();
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

    size_t build_corner_pips(size_t x, size_t y);
    size_t build_qsb_pips(size_t x, size_t y);
    size_t build_qcb_pips(size_t x, size_t y);
    size_t build_clb_pips(size_t x, size_t y);
    size_t build_iob_pips(size_t x, size_t y);

    size_t build_clb_bels(size_t x, size_t y);
    size_t build_iob_bels(size_t x, size_t y);
};

// Build a mesh of tiles
mesh_t mesh_build(mesh_config_t &cfg);
wire_mesh_t mesh_build_wires(Context *ctx, const mesh_t &mesh, const mesh_config_t &cfg);

// Print mesh
void mesh_print(const mesh_t &mesh);

NEXTPNR_NAMESPACE_END

#endif
