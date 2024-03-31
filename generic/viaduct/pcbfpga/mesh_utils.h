/*
 *  nextpnr -- Next Generation Place and Route
 *
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

#ifndef MESH_UTILS_H
#define MESH_UTILS_H

#include "nextpnr_namespaces.h"

NEXTPNR_NAMESPACE_BEGIN

namespace mesh_utils {
    // Is on device edge -> 1 tile from edge
    bool is_on_edge(const int x, const int y, const int X, const int Y) {
        return ((x == 0) || (x == (X - 1)) || (y == 0) || (y == (Y - 1)));
    }
    // Is inside mesh perimeter -> 2 tiles from edge
    bool is_in_mesh_perimeter(const int x, const int y, const int X, const int Y) {
        return ((x > 1) && (x < (X - 2)) && (y > 1) && (y < (Y - 2)));
    }
    // Is on mesh perimeter corner -> 2 tiles from edge in corners
    bool is_on_mesh_perimeter_corner(const int x, const int y, const int X, const int Y) {
        return ((x == 1) && (y == 1)) || ((x == (X - 2)) && (y == 1)) || ((x == 1) && (y == (Y - 2))) || ((x == (X - 2)) && (y == (Y - 2)));
    }
    // Is on mesh perimeter -> 2 tiles from edge
    bool is_on_mesh_perimeter(const int x, const int y, const int X, const int Y) {
        return !is_on_edge(x, y, X, Y) && !is_in_mesh_perimeter(x, y, X, Y);
    }
    // Is on device corner
    bool is_corner(const int x, const int y, const int X, const int Y) {
        return ((x == 0) && (y == 0)) || ((x == (X - 1)) && (y == 0)) || ((x == 0) && (y == (Y - 1))) || ((x == (X - 1)) && (y == (Y - 1)));
    }
    // Is IOB -> On edge and every other tile
    bool is_io(const int x, const int y, const int X, const int Y) {
        return !is_corner(x, y, X, Y) && is_on_edge(x, y, X, Y) && !((x % 2 == 0) ^ (y % 2 == 0));
    }
    // Is CLB -> Not on the edge of the device and every even tile
    bool is_clb(const int x, const int y, const int X, const int Y) { 
        return !is_on_edge(x, y, X, Y) && ((x % 2 == 0) && (y % 2 == 0));
    }
    // Is SWB -> Every odd tile
    bool is_swb(const int x, const int y, const int X, const int Y) {
        // Every odd tile
        return (x % 2 == 1) && (y % 2 == 1);
    }
    // Is QSB -> Inside mesh perimeter and is swb
    bool is_qsb(const int x, const int y, const int X, const int Y) {
        return is_in_mesh_perimeter(x, y, X, Y) && is_swb(x, y, X, Y);
    }
    // Is TSB -> On mesh perimeter but not corner and is swb
    bool is_tsb(const int x, const int y, const int X, const int Y) {
        return is_on_mesh_perimeter(x, y, X, Y) && !is_on_mesh_perimeter_corner(x, y, X, Y) && is_swb(x, y, X, Y);
    }
    // Is DSB -> On mesh perimeter corner and is swb
    bool is_dsb(const int x, const int y, const int X, const int Y) {
        return is_on_mesh_perimeter_corner(x, y, X, Y) && is_swb(x, y, X, Y);
    }
    // Is CCB -> Inside mesh perimeter and not swb or clb
    bool is_ccb(const int x, const int y, const int X, const int Y) {
        return is_in_mesh_perimeter(x, y, X, Y) && !is_swb(x, y, X, Y) && !is_clb(x, y, X, Y);
    }
    // Is ICB -> On mesh perimeter but not conre and not tsb
    bool is_icb(const int x, const int y, const int X, const int Y) {
        return is_on_mesh_perimeter(x, y, X, Y) && !is_on_mesh_perimeter_corner(x, y, X, Y) && !is_tsb(x, y, X, Y);
    }
}

NEXTPNR_NAMESPACE_END

#endif