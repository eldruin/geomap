# --------------------------------------------------------------------
#                             edge flags
# --------------------------------------------------------------------

# note that the highest bits are reserved for the C++ code:
#     static const unsigned int IS_BRIDGE = 0x8000000;

# edge protection:
BORDER_PROTECTION  = 1
SCISSOR_PROTECTION = 2
CONTOUR_PROTECTION = 4 # cf. maputils.protectFace()
CUSTOM_PROTECTION  = 8

# sinology:
COLUMN_PROTECTION = 16

import geomap
ALL_PROTECTION = geomap.GeoMap.Edge.ALL_PROTECTION
assert geomap.GeoMap.Edge.BORDER_PROTECTION == BORDER_PROTECTION
assert ALL_PROTECTION & 31 == 31, "must include or'ed values of the ones above"

# delaunay:
CONTOUR_SEGMENT  = 256
WEAK_CHORD       = 512
IS_BARB          = 1024
START_NODE_ADDED = 8192
END_NODE_ADDED   = 16384

# tools/IntelligentScissors:
CURRENT_CONTOUR = 2048

# alphashapes:
ALPHA_MARK = 4096 # used for both edges and faces, see below

EDGE_USER = 0x100000

# --------------------------------------------------------------------
#                             face flags
# --------------------------------------------------------------------

# note that the highest bits are reserved for the C++ code:
#     static const unsigned int BOUNDING_BOX_VALID = 0x8000000;
#     static const unsigned int AREA_VALID         = 0x4000000;

# delaunay:
OUTER_FACE = 1

# tools/ActivePaintbrush:
PROTECTED_FACE = 2 # flag which leads to CONTOUR_PROTECTION

# alphashapes:
#ALPHA_MARK = 4096 # see above
CORNER_FACE = 32

# maputils:
SRG_SEED = 8
SRG_BORDER = 16

# levelcontours:
BACKGROUND_FACE = 1 # cf. OUTER_FACE
FOREGROUND_FACE = 32

FACE_USER = 0x100000

if __name__ == "__main__":
    for name in dir():
        value = eval(name)
        if isinstance(value, int):
            print "%20s: %8x (%d)" % (name, value, value)
