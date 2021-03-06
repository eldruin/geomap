##########################################################################
#
#                Copyright 2007-2019 by Hans Meine
#
#     Permission is hereby granted, free of charge, to any person
#     obtaining a copy of this software and associated documentation
#     files (the "Software"), to deal in the Software without
#     restriction, including without limitation the rights to use,
#     copy, modify, merge, publish, distribute, sublicense, and/or
#     sell copies of the Software, and to permit persons to whom the
#     Software is furnished to do so, subject to the following
#     conditions:
#
#     The above copyright notice and this permission notice shall be
#     included in all copies or substantial portions of the
#     Software.
#
#     THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND
#     EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
#     OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
#     NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
#     HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
#     WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
#     FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
#     OTHER DEALINGS IN THE SOFTWARE.
#
##########################################################################

import sys, copy
import numpy, vigra, geomap, maputils, flag_constants, progress
from geomap import Vector2, Point2D

__all__ = ["levelSetMap", "marchingSquares"]

# TODO:
# 1) better border handling in followContour() (predictorStep, correctorStep)
# 3) make followContour() check for both possible intersections
#    and remove the duplicate filtering (cf. levelcontours_both_intersections.diff)
# 4) don't store zc positions in a GeoMap, but use a PositionedMap and
#    cancel out points instead of starting new edges everytime
#    (-> consecutive node and edge labels)
# 5) try larger stepsizes and a good criterion for decreasing h
#    (e.g. the size of the corrector step?!)

def findZeroCrossingsOnGrid(siv, level, minDist = 0.1):
    result = []
    existing = geomap.PositionedMap()
    minDist2 = minDist*minDist

    def addIntersection(p):
        if not existing(p, minDist2):
            result.append(p)
            existing.insert(p, True)

    for y in range(siv.height()-1):
        for x in range(siv.width()-1):
            coeff = siv.coefficients(x, y)

            xPoly = [coeff[k,0] for k in range(coeff.width())]
            xPoly[0] -= level
            try:
                for k in vigra.polynomialRealRoots(xPoly):
                    if k < 0.0 or k >= 1.0:
                        continue
                    addIntersection(Vector2(x+k, y))
            except Exception, e:
                sys.stderr.write("WARNING: no convergence in polynomialRealRoots(%s):\n  %s\n" % (xPoly, e))

            yPoly = [coeff[0,k] for k in range(coeff.height())]
            yPoly[0] -= level
            try:
                for k in vigra.polynomialRealRoots(yPoly):
                    if k < 0.0 or k >= 1.0:
                        continue
                    addIntersection(Vector2(x, y+k))
            except Exception, e:
                sys.stderr.write("WARNING: no convergence in polynomialRealRoots(%s):\n  %s\n" % (yPoly, e))
    return result

# --------------------------------------------------------------------

def tangentDir(siv, pos):
    result = Vector2(-siv.dy(pos[0], pos[1]), siv.dx(pos[0], pos[1]))
    return result / numpy.linalg.norm(result)

def predictorStep(siv, pos, h):
    """predictorStep(siv, pos, h) -> Vector2

    Step distance h from pos in direction of tangent (perpendicular
    to gradient).  Returns None if that point is outside the
    SplineImageView."""

    return pos + h*tangentDir(siv, pos)

def correctorStep(siv, level, pos, epsilon = 1e-8):
    """Perform corrector step, i.e. perform 1D iterative Newton method in
    direction of gradient in order to return to zero level (with
    accuracy given by epsilon)."""

    x, y = pos
    n = Vector2(siv.dx(x, y), siv.dy(x, y))
    n /= numpy.linalg.norm(n)

    for k in range(100):
        value = siv(x, y) - level
        if abs(value) < epsilon:
            break

        g = numpy.dot(Vector2(siv.dx(x, y), siv.dy(x, y)), n)
        if not g:
            sys.stderr.write("WARNING: correctorStep: zero gradient!\n")
            break # FIXME: return None instead?
        correction = -value * n / g

        # prevent too large steps (i.e. if norm(g) is small):
        if correction.squaredMagnitude() > 0.25:
            correction /= 20*numpy.linalg.norm(correction)

        x += correction[0]
        y += correction[1]

        if not siv.isInside(x, y):
            return None # out of range

    return Vector2(x, y)

def predictorCorrectorStep(siv, level, pos, h, epsilon):
    while abs(h) > 1e-6:
        p1 = predictorStep(siv, pos, h)
        if not siv.isInside(p1[0], p1[1]):
            #print "predictor went outside at", p1
            return p1, None

        p2 = correctorStep(siv, level, p1, epsilon)
        if not p2 or squaredNorm(p2 - p1) > h:
            h /= 2.0
            continue

        h *= 2
        return p2, h

    sys.stderr.write(
        "WARNING: predictorCorrectorStep: not converged at %s!\n" % p2)
    return p2, None

# --------------------------------------------------------------------

def followContour(siv, level, geomap, nodeLabel, h):
    correctorEpsilon = 1e-6
    nodeCrossingDist = 0.01

    #global pos, ip, poly, startNode, diff, npos, nip, intersection
    startNode = geomap.node(nodeLabel)
    pos = startNode.position()
    ix = int(pos[0])
    iy = int(pos[1])
    poly = [pos]
    while True:
        npos, nh = predictorCorrectorStep(siv, level, pos, h, correctorEpsilon)
        h = max(min(h, nh), 1e-5)
        nix = int(npos[0])
        niy = int(npos[1])
        if nix != ix or niy != iy:
            # determine grid intersection:
            diff = npos - pos
            if nix != ix:
                intersectionX = round(npos[0])
                intersectionY = pos[1]+(intersectionX-pos[0])*diff[1]/diff[0]
            else:
                intersectionY = round(npos[1])
                intersectionX = pos[0]+(intersectionY-pos[1])*diff[0]/diff[1]
            intersection = Vector2(intersectionX, intersectionY)

            # connect to crossed Node:
            endNode = geomap.nearestNode(intersection, nodeCrossingDist)
            if not endNode:
                sys.stderr.write("WARNING: level contour crossing grid at %s without intersection Node!\n" % repr(intersection))
            elif endNode.label() != startNode.label() or len(poly) >= 3:
                # FIXME: better criterion than len(poly) would be poly.length()
                poly.append(endNode.position())
                geomap.addEdge(startNode, endNode, poly)
                if not endNode.degree() % 2:
                    return
                poly = [endNode.position()]
                startNode = endNode

            ix = nix
            iy = niy

        if nh is None:
            return # out of image range (/no convergence)
        poly.append(npos)
        pos = npos

def levelSetMap(image, level = 0, sigma = None):
    siv = hasattr(image, "siv") and image.siv or vigra.SplineImageView3(image)

    zc = findZeroCrossingsOnGrid(siv, level)
    result = geomap.GeoMap(zc, [], image.size())

    msg = progress.StatusMessage("- following level set contours")
    next = progress.ProgressHook(msg).rangeTicker(result.nodeCount)

    for node in result.nodeIter():
        next()
        if node.isIsolated():
            followContour(siv, level, result, node.label(), 0.1)

    maputils.mergeDegree2Nodes(result)
    result = maputils.copyMapContents( # compress labels and simplify polygons
        result, edgeTransform = lambda e: \
        geomap.simplifyPolygon(e, 0.05, 0.2))[0]
    #maputils.connectBorderNodes(result, 0.01)

    result.sortEdgesEventually(0.4, 0.01)
    result.initializeMap()
    return result

# --------------------------------------------------------------------

def marchingSquares(image, level = 0, variant = True, border = True,
                    initialize = True, markOuter = 1):
    """Return a new GeoMap with sub-pixel level contours extracted by
    the marching squares method.  (Pixels with values < level are
    separated from pixels >= level.)

    If the image does not have an attribute 'siv', standard linear
    interpolation is used.  If image.siv exists, it should be a
    SplineImageView that is used for the Newton-Raphson method to
    perform another subsequent sub-pixel correction.

    The optional parameter `variant` determines the handling of the
    ambiguous diagonal configuration:

    `variant` = True (default)
      always let the two sampling points above `level` be connected

    `variant` = False
      always let the two opposite sampling points < `level` be connected

    `variant` = SplineImageView(...)
      for each ambiguous configuration, check the midpoint of the
      square; then handle as if variant = (midpoint >= level)

    If `initialize` is a true value (default), the map will be
    initialized.

    If markOuter is != 0, the faces above(outer == 1) / below(outer == -1)
    the threshold are marked with the OUTER_FACE flag (this only works
    if the map is initialized)."""

    connections1 = ((1, 0), (0, 2), (1, 2), (3, 1), (3, 0), (0, 2), (3, 1), (3, 2), (2, 3), (1, 0), (2, 3), (0, 3), (1, 3), (2, 1), (2, 0), (0, 1))
    connections2 = ((1, 0), (0, 2), (1, 2), (3, 1), (3, 0), (0, 1), (3, 2), (3, 2), (2, 3), (1, 3), (2, 0), (0, 3), (1, 3), (2, 1), (2, 0), (0, 1))
    configurations = (0, 0, 1, 2, 3, 4, 5, 7, 8, 9, 11, 12, 13, 14, 15, 16, 16)

    result = geomap.GeoMap(image.shape)

    def addNodeDirectX(x, y, ofs):
        pos = Vector2(x+ofs, y)
        # out of three successive pixels, the middle one may be the
        # threshold, then we would get duplicate points already in the
        # horizontal pass:
        node = result.nearestNode(pos, 1e-8)
        return node or result.addNode(pos)

    def addNodeDirectY(x, y, ofs):
        pos = Vector2(x, y+ofs)
        node = result.nearestNode(pos, 1e-8) # already exists? (e.g. hNodes?)
        return node or result.addNode(pos)

    def addNodeNewtonRefinementX(x, y, ofs):
        for i in range(100):
            o = -(image.siv(x+ofs, y)-level) / image.siv.dx(x+ofs, y)
            if abs(o) > 0.5:
                o = vigra.sign(o)*0.05
            ofs += o
            if ofs <= 0 or ofs >= 1:
                ofs -= o
                break
            if abs(o) < 1e-4:
                break
        return addNodeDirectX(x, y, ofs)

    def addNodeNewtonRefinementY(x, y, ofs):
        for i in range(100):
            o = -(image.siv(x, y+ofs)-level) / image.siv.dy(x, y+ofs)
            if abs(o) > 0.5:
                o = vigra.sign(o)*0.05
            ofs += o
            if ofs <= 0 or ofs >= 1:
                ofs -= o
                break
            if abs(o) < 1e-4:
                break
        return addNodeDirectY(y, y, ofs)

    if hasattr(image, "siv"):
        addNodeX = addNodeNewtonRefinementX
        addNodeY = addNodeNewtonRefinementY
    else:
        addNodeX = addNodeDirectX
        addNodeY = addNodeDirectY

    hNodes = vigra.Image(image.shape, numpy.uint32)
    v1 = image[:-1]
    v2 = image[1:]
    ofs = (level - v1)/(v2 - v1)
    for x, y in numpy.transpose(numpy.nonzero((v1 < level) != (v2 < level))):
        hNodes[x, y] = addNodeX(x, y, ofs).label()

    vNodes = vigra.Image(image.shape, numpy.uint32)
    v1 = image[:,:-1]
    v2 = image[:,1:]
    ofs = (level - v1)/(v2 - v1)
    for x, y in numpy.transpose(numpy.nonzero((v1 < level) != (v2 < level))):
        vNodes[x, y] = addNodeY(x, y, ofs).label()

    nodes = (hNodes, vNodes, vNodes, hNodes)
    offsets = numpy.array(((0, 0), (0, 0), (1, 0), (0, 1)))

    defaultConnections = connections1
    if variant == False:
        defaultConnections = connections2
    if isinstance(variant, bool):
        variant = None

    configurations = ((image[:-1,:-1] < level) +
                      (image[ 1:,:-1] < level)*2 +
                      (image[:-1, 1:] < level)*4 +
                      (image[ 1:, 1:] < level)*8)
    for x, y in numpy.transpose(numpy.nonzero(configurations)):
        config = configurations[x,y]

        connections = connections1
        if variant is not None and config in (6, 9):
            if variant(x + 0.5, y + 0.5) < level:
                connections = connections2

        for s, e in connections[
            configurations[config]:configurations[config+1]]:
            startNode = result.node(int(nodes[s][tuple(offsets[s] + (x, y))]))
            endNode   = result.node(int(nodes[e][tuple(offsets[e] + (x, y))]))
            if startNode != endNode:
                result.addEdge(startNode, endNode,
                               [startNode.position(), endNode.position()])

    maputils.mergeDegree2Nodes(result) # node suppression
    result = maputils.copyMapContents(result)[0] # compress edge labels

    if border:
        maputils.connectBorderNodes(result, 0.5)
        result.sortEdgesEventually(0.4, 0.01)

    if not initialize:
        return result

    result.initializeMap()
    if markOuter:
        markOuter = markOuter > 0
        it = result.faceIter()
        if border:
            it.next().setFlag(flag_constants.OUTER_FACE)
        for face in it:
            face.setFlag(flag_constants.OUTER_FACE,
                         (face.contours().next().label() < 0) == markOuter)

    return result

# --------------------------------------------------------------------

# from vigra import addPathFromHere
# addPathFromHere("../evaluation/")
# import edgedetectors

# def levelSetMap(image, level = 0, sigma = None):
#     ed = edgedetectors.EdgeDetector(
#         bi = "Thresholding", s1 = sigma, nonmax = "zerosSubPixel",
#         threshold = level)
#     result, _, _ = ed.computeMap(image)
#     maputils.mergeDegree2Nodes(result)
#     return result

# --------------------------------------------------------------------

"""
import numpy
#vigra.ScalarImage((200,100), numpy.uint8)
import vigra
import sys
sys.path.append("/home/hmeine/vigra/vigranumpy/private/subpixelWatersheds")
import levelcontours
i = vigra.readImage("/home/hmeine/Testimages/walk.png")
lc = levelcontours.marchingSquares(i[...,0], 200)

import vigra.pyqt
vigra.pyqt.showImage(i)

import mapdisplay
"""
