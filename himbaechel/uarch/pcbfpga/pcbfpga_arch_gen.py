from os import path
import sys
sys.path.append(path.join(path.dirname(__file__), "../.."))
from himbaechel_dbgen.chip import *

X = 7
Y = 7

Rchan = 1
N_io = 1

SlicesPerCLB = 4

# Create a IOB tile type
def create_iob_tiletype(ch):
    tt = ch.create_tile_type("IOB")

    inputs = []
    outputs = []
    for i in range(N_io):
        tt.create_wire(f"IO{i}_EN", "IO_EN")
        tt.create_wire(f"IO{i}_I", "IO_I")
        tt.create_wire(f"IO{i}_O", "IO_O")
        tt.create_wire(f"IO{i}_PAD", "IO_PAD")
        inputs += [f"IO{i}_EN", f"IO{i}_I"]
        outputs += [f"IO{i}_O", ]
    for i in range(N_io):
        io = tt.create_bel(f"IO{i}", "IOB", z=i)
        tt.add_bel_pin(io, "I", f"IO{i}_I", PinType.INPUT)
        tt.add_bel_pin(io, "EN", f"IO{i}_EN", PinType.INPUT)
        tt.add_bel_pin(io, "O", f"IO{i}_O", PinType.OUTPUT)
        tt.add_bel_pin(io, "PAD", f"IO{i}_PAD", PinType.INOUT)

    return tt

# Create a QSB tile type
def create_qsb_tiletype(ch):
    tt = ch.create_tile_type("QSB")

    for i in range(Rchan):
        tt.create_wire(f"N_{i}", "CHAN_N")
        tt.create_wire(f"E_{i}", "CHAN_E")
        tt.create_wire(f"S_{i}", "CHAN_S")
        tt.create_wire(f"W_{i}", "CHAN_W")

    for i in ["N", "E", "S", "W"]:
        for j in ["N", "E", "S", "W"]:
            if i == j:
                continue
            for k in range(N_io):
                tt.create_pip(f"{i}_{k}", f"{j}_{k}")

    return tt

# Create a CLB tile type
def create_clb_tiletype(ch):
    tt = ch.create_tile_type("CLB")

    # Create wires
    for s in range(SlicesPerCLB):
        # Per slice
        sidx = s * 3

        # First LUT3 inputs
        tt.create_wire(f"SLICE{sidx}_LUT3_1_I0", "LUT_INPUT")
        tt.create_wire(f"SLICE{sidx}_LUT3_1_I1", "LUT_INPUT")
        tt.create_wire(f"SLICE{sidx}_LUT3_1_I2", "LUT_INPUT")

        # Second LUT3 inputs
        tt.create_wire(f"SLICE{sidx}_LUT3_2_I0", "LUT_INPUT")
        tt.create_wire(f"SLICE{sidx}_LUT3_2_I12", "LUT_INPUT")
        tt.create_wire(f"SLICE{sidx}_LUT3_2_I23", "LUT_INPUT")

        # LUT3 outputs
        tt.create_wire(f"SLICE{sidx}_LUT3_1_RES", "LUT_OUT")
        tt.create_wire(f"SLICE{sidx}_LUT3_2_RES", "LUT_OUT")

        # LUT4 output
        tt.create_wire(f"SLICE{sidx}_LUT4_F", "LUT4_OUT")

        # FF input and output
        tt.create_wire(f"SLICE{sidx}_FF_D", "FF_DATA")
        tt.create_wire(f"SLICE{sidx}_FF_Q", "FF_OUT")

        # Slice inputs
        for i in range(4):
            tt.create_wire(f"SLICE{sidx}_I{i}", "SLICE_IN")
        tt.create_wire(f"SLICE{sidx}_D", "SLICE_IN")

        # Slice outputs
        tt.create_wire(f"SLICE{sidx}_GF", "SLICE_OUT")
        tt.create_wire(f"SLICE{sidx}_FQ", "SLICE_OUT")
    
        # Create first LUT3
        lut = tt.create_bel(f"SLICE{sidx}_LUT3_1", f"LUT3", sidx)
        tt.add_bel_pin(lut, "I0", f"SLICE{sidx}_LUT3_1_I0", PinType.INPUT)
        tt.add_bel_pin(lut, "I1", f"SLICE{sidx}_LUT3_1_I1", PinType.INPUT)
        tt.add_bel_pin(lut, "I2", f"SLICE{sidx}_LUT3_1_I2", PinType.INPUT)
        tt.add_bel_pin(lut, "O", f"SLICE{sidx}_LUT3_1_RES", PinType.OUTPUT)
        
        # Create second LUT3
        lut = tt.create_bel(f"SLICE{sidx}_LUT3_2", f"LUT3", sidx+1)
        tt.add_bel_pin(lut, "I0", f"SLICE{sidx}_LUT3_2_I0", PinType.INPUT)
        tt.add_bel_pin(lut, "I12", f"SLICE{sidx}_LUT3_2_I12", PinType.INPUT)
        tt.add_bel_pin(lut, "I23", f"SLICE{sidx}_LUT3_2_I23", PinType.INPUT)
        tt.add_bel_pin(lut, "O", f"SLICE{sidx}_LUT3_2_RES", PinType.OUTPUT)

        # Create FF
        ff = tt.create_bel(f"SLICE{sidx}_FF", f"DFF", sidx+2)
        tt.add_bel_pin(ff, "D", f"SLICE{sidx}_FF_D", PinType.INPUT)
        tt.add_bel_pin(ff, "Q", f"SLICE{sidx}_FF_Q", PinType.OUTPUT)

        # Create pips

        # FF D select
        tt.create_pip(f"SLICE{sidx}_LUT4_F", f"SLICE{sidx}_FF_D")
        tt.create_pip(f"SLICE{sidx}_D", f"SLICE{sidx}_FF_D")

        # FQ select
        tt.create_pip(f"SLICE{sidx}_LUT4_F", f"SLICE{sidx}_FQ")
        tt.create_pip(f"SLICE{sidx}_FF_Q", f"SLICE{sidx}_FQ")

        # GF select
        tt.create_pip(f"SLICE{sidx}_LUT4_F", f"SLICE{sidx}_GF")
        tt.create_pip(f"SLICE{sidx}_LUT3_2_RES", f"SLICE{sidx}_GF")

    return tt

# Create a QCB tile type
def create_qcb_tiletype(ch):
    tt = ch.create_tile_type("QCB")

    for i in range(Rchan):
        tt.create_wire(f"CHAN_{i}", "CHAN")
        tt.create_wire(f"QSB1_{i}", "QSB")
        tt.create_wire(f"QSB2_{i}", "QSB")

        # Dummy pips between QSB1/2 and CHAN
        tt.create_pip(f"QSB1_{i}", f"CHAN_{i}")
        tt.create_pip(f"QSB2_{i}", f"CHAN_{i}")
        tt.create_pip(f"CHAN_{i}", f"QSB1_{i}")
        tt.create_pip(f"CHAN_{i}", f"QSB2_{i}")

    return tt


def main():
    # Create a new chip
    ch = Chip("test", "EX1", X, Y)

    # Get constids
    ch.strs.read_constids(path.join(path.dirname(__file__), "constids.inc"))
    
    # Generate the device tiles
    tt_null = ch.create_tile_type("   ")
    tt_cor = ch.create_tile_type("COR")
    tt_iob = create_iob_tiletype(ch)
    tt_qsb = create_qsb_tiletype(ch)
    tt_clb = create_clb_tiletype(ch)
    tt_qcb = create_qcb_tiletype(ch)
    device_arch = [["   " for x in range(X)] for y in range(Y)]
    for y in range(Y):
        for x in range(X):
            if x == 0 or x == X-1: # left/right side IO
                if (y > 1 and y < Y-2) and y % 2 == 0: # IO
                    device_arch[y][x] = "IOB"
                else:
                    device_arch[y][x] = "   "
            elif (y == 0 or y == Y-1): # top/bottom side IO
                if (x > 1 and x < X-2) and x % 2 == 0: # IO
                    device_arch[y][x] = "IOB"
                else:
                    device_arch[y][x] = "   "
            else: # Core
                if (x == 1 or x == X-2) and (y == 1 or y == Y-2):
                    device_arch[y][x] = "COR"
                elif x % 2 == 1 and y % 2 == 1:
                    device_arch[y][x] = "QSB"
                elif x % 2 == 0 and y % 2 == 0:
                    device_arch[y][x] = "CLB"
                else:
                    device_arch[y][x] = "QCB"
            ch.set_tile_type(x, y, device_arch[y][x])

    # Print device
    print("Device Architecture:")
    for y in range(Y):
        for x in range(X):
            print(device_arch[y][x], end=" ")
        print()

    # Generate nodes -> connects the tiles wires
    # Only QCB creates nodes
    for x in range(X):
        for y in range(Y):
            if device_arch[y][x] != "QCB":
                continue
            
            local_nodes = []

            # Check if neighbors are QSBs
            if x > 0 and device_arch[y][x-1] == "QSB": # Left of QCB
                for i in range(Rchan):
                    local_nodes.append([NodeWire(x, y, f"QSB1_{i}"), NodeWire(x-1, y, f"W_{i}")])
            if x < X-2 and device_arch[y][x+1] == "QSB": # Right of QCB
                for i in range(Rchan):
                    local_nodes.append([NodeWire(x, y, f"QSB2_{i}"), NodeWire(x+1, y, f"E_{i}")])
            if y > 0 and device_arch[y-1][x] == "QSB": # Top of QCB
                for i in range(Rchan):
                    local_nodes.append([NodeWire(x, y, f"QSB1_{i}"), NodeWire(x, y-1, f"S_{i}")])
            if y < Y-2 and device_arch[y+1][x] == "QSB": # Bottom of QCB
                for i in range(Rchan):
                    local_nodes.append([NodeWire(x, y, f"QSB2_{i}"), NodeWire(x, y+1, f"N_{i}")])

            for n in local_nodes:
                #print(n)
                ch.add_node(n)
    
    # Generate corner nodes
    corner_nodes = []
    for i in range(Rchan):
        # Top Left
        local_nodes.append([NodeWire(1, 2, f"QSB1_{i}"), NodeWire(2, 1, f"QSB1_{i}")])

        # Top Right
        local_nodes.append([NodeWire(X-2, 2, f"QSB1_{i}"), NodeWire(X-3, 1, f"QSB2_{i}")])

        # Bottom Left
        local_nodes.append([NodeWire(1, Y-3, f"QSB2_{i}"), NodeWire(2, Y-2, f"QSB1_{i}")])

        # Bottom Right
        local_nodes.append([NodeWire(X-2, Y-3, f"QSB2_{i}"), NodeWire(X-3, Y-2, f"QSB2_{i}")])

    for n in corner_nodes:
        ch.add_node(n)
    
    # Write bba
    ch.write_bba(sys.argv[1])

if __name__ == '__main__':
    main()
