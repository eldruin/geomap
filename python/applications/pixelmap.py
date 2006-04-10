from vigra import *
from hourglass import Polygon
addPathFromHere('../cellimage')
from cellimage import GeoMap, CellType
from map import Map

__all__ = ["pixelMapData", "pixelMap2subPixelMap",
           "crackEdges2MidCracks", "cannyEdgeMap", "pixelWatershedMap"]

def pixelMapData(geomap, scale = 1.0, offset = Vector2(0, 0)):
    """pixelMapData(geomap, scale = 1.0, offset = Vector2(0, 0))

    Extracts node positions and edge geometry from a GeoMap object.
    For nodes, this function simply calculates their center of mass.
    All positions are shifted by the optional offset and then scaled
    with the given factor."""

    nodes = [None] * (geomap.maxNodeLabel() + 1)
    for node in geomap.nodes:
        ul = node.bounds.upperLeft()
        center = Vector2(*ul) + Vector2(node.bounds.width() - 1,
                                        node.bounds.height() - 1) / 2
        nodes[node.label] = (center+offset) * scale

    edges = [None] * (geomap.maxEdgeLabel() + 1)
    for edge in geomap.edges:
        points = [(Vector2(*p)+offset) * scale for p in iter(edge.start)]
        startNodeLabel = edge.start.startNodeLabel()
        endNodeLabel = edge.end.startNodeLabel()
        points.insert(0, nodes[startNodeLabel])
        points.append(nodes[endNodeLabel])
        edges[edge.label] = (
            startNodeLabel, endNodeLabel, Polygon(points))
    return nodes, edges

def pixelMap2subPixelMap(geomap, scale = 1.0, offset = Vector2(0, 0),
                         labelImageSize = None):
    """pixelMap2subPixelMap(geomap, scale = 1.0, offset = Vector2(0, 0), labelImageSize = None)

    Uses pixelMapData() to extract the pixel-geomap's geometry and
    returns a new subpixel-Map object initialized with it.  The
    labelImageSize defaults to the (scaled) pixel-based geomap's
    cellImage.size().  See also the documentation of pixelMapData()."""
    
    nodes, edges = pixelMapData(geomap, scale, offset)
    if labelImageSize == None:
        labelImageSize = geomap.cellImage.size() * scale
    return Map(nodes, edges, labelImageSize,
               performBorderClosing = False, performEdgeSplits = False)

def crackEdges2MidCracks(subpixelMap, skipEverySecond = False):
    """crackEdges2MidCracks(subpixelMap, skipEverySecond = False)

    Changes all edge geometry in-place, setting one point on the
    middle of each segment. Set skipEverySecond to True if each pixel
    crack is represented with two segments."""
    
    for edge in subpixelMap.edgeIter():
        p = Polygon()
        step = skipEverySecond and 2 or 1
        p.append(edge[0])
        for i in range(0, len(edge)-1, step):
            p.append((edge[i]+edge[i+step])/2)
        p.append(edge[-1])
        edge.swap(p) # FIXME: use edge.setGeometry as soon as that's finished

def cannyEdgeImageThinning(img):
    lut = [0]*256
    for k in [183, 222, 123, 237, 219, 111, 189, 246, 220, 115, 205,
              55, 103, 157, 118, 217]:
        lut[k] = 1
    res = GrayImage(img.size())
    res[1:-1,1:-1].copyValues(img[1:-1,1:-1])
    for y in range(1, res.height()-1):
        for x in range(1, res.width()-1):
            if res[x,y]:
                res[x,y] = 1
                continue # no edge pixel
            n = [k for k in res.neighborhood8((x,y))]
            conf = 0
            pt, nt = [], []
            for k in range(8):
                conf |= int(n[k] == 0) << k
                if n[k] == 0 and n[k-1] != 0:
                    nt.append(k)
                if n[k] != 0 and n[k-1] == 0:
                    pt.append(k)
                    lab = n[k]
            if len(pt) != 1:
                if lut[conf]:
                    res[x,y] = 1
                continue
            if nt[0] > pt[0]:
                pt[0] += 8
            if pt[0]-nt[0] >= 3:
                res[x,y] = 1
    return res[1:-1,1:-1].clone()

def cannyEdgeMap(image, scale, thresh):
    """cannyEdgeMap(image, scale, thresh)

    Returns a subpixel-Map object containing thinned canny edges
    obtained from cannyEdgeImage(image, scale, thresh).
    (Internally creates a pixel GeoMap first.)"""
    
    edgeImage = cannyEdgeImage(image, scale, thresh)
    edgeImage = cannyEdgeImageThinning(edgeImage)
    geomap = GeoMap(edgeImage, 0, CellType.Line)
    spmap = pixelMap2subPixelMap(
        geomap, offset = Vector2(1,1), labelImageSize = image.size())
    return spmap

def crackEdgeMap(labelImage, midCracks = True):
    """crackEdgeMap(labelImage, midCracks = True)

    Returns a subpixel-Map containing crack-edge contours extracted
    from the given labelImage.  If the optional parameter 'midCracks'
    is True(default), the resulting edges consist of the connected
    midpoints of the cracks, not of the crack segments themselves."""

    print "- creating pixel-based GeoMap..."
    ce = regionImageToCrackEdgeImage(transformImage(labelImage, "\l x:x+1"), 0)
    geomap = GeoMap(ce, 0, CellType.Line)

    print "- converting pixel-based GeoMap..."
    result = pixelMap2subPixelMap(
        geomap, 0.5, labelImageSize = (geomap.cellImage.size()-Size2D(3,3))/2)
    if midCracks:
        crackEdges2MidCracks(result, True)
    return result

def pixelWatershedMap(biImage, crackEdges = 4, midCracks = True):
    """pixelWatershedMap(biImage, crackEdges = 4, midCracks = True)

    Performs a watershed segmentation on biImage and returns a
    subpixel-Map containing the resulting contours.  The type of
    watershed segmentation depends on the 'crackEdges' parameter:

    0: 8-connected edges on 4-connected background
    4: crack edges between 4-connected watershed regions
    8: crack edges between 8-connected watershed regions
       (8-connected regions will be separated in the result ATM)

    If midCracks is True(default), the resulting edges consist of the
    connected midpoints of the cracks, not of the crack segments
    themselves."""
    
    if crackEdges:
        print "- Union-Find watershed segmentation..."
        lab, count = eval('watershedUnionFind' + str(crackEdges))(biImage)
        return crackEdgeMap(lab, midCracks)

    print "- watershed segmentation..."
    lab, count = watershedSegmentation(biImage, KeepContours)

    print "- creating pixel-based GeoMap..."
    geomap = GeoMap(lab, 0, CellType.Vertex)

    print "- converting pixel-based GeoMap..."
    return pixelMap2subPixelMap(
        geomap, labelImageSize = (geomap.cellImage.size()-Size2D(4,4)))
