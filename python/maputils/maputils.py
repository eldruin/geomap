import hourglass, sys
from vigra import * # FIXME?

# --------------------------------------------------------------------
#                            edge protection
# --------------------------------------------------------------------

BORDER_PROTECTION = 1
SCISSOR_PROTECTION = 2

class EdgeProtection(object):
    def __init__(self, map):
        self._attachedHooks = (
            map.addMergeFacesCallbacks(self.preRemoveEdge, None),
            map.addRemoveBridgeCallbacks(self.preRemoveEdge, None),
            map.addMergeEdgesCallbacks(self.preMergeEdges, None))

    def detachHooks(self):
        for cb in self._attachedHooks:
            cb.disconnect()

    def preRemoveEdge(self, dart):
        "do not allow removal of protected edges"
        return not dart.edge().flags()

    def preMergeEdges(self, dart):
        "only allow edge merging if the edges carry the same flags"
        return (dart.edge().flags() ==
                dart.clone().nextSigma().edge().flags())

# --------------------------------------------------------------------
#                        map creation helpers
# --------------------------------------------------------------------

def addFlowLinesToMap(edges, map):
    """addFlowLinesToMap(edges, map)

    This function expects `edges` to be a list of
    SubPixelWatersheds.edge() return values and adds edges for each
    flowline.  It contains some special handling of flowlines:

    * Additional Nodes may be added for flowlines that did not end in
      a maximum - extra care is taken not to insert multiple Nodes at
      (nearly) the same position.

    * Self-loops with area zero are not added.

    Returns edgeTriples that could not be added to the GeoMap."""
    
    # Node 0 conflicts with our special handling of 0 values:
    assert not map.node(0), \
           "addFlowLinesToMap: Node with label zero should not exist!"

    result = []
    for edgeLabel, edgeTriple in enumerate(edges):
        if not edgeTriple:
            continue
        startNodeLabel = edgeTriple[0]
        endNodeLabel = edgeTriple[1]
        points = hourglass.Polygon(edgeTriple[2]) # don't modify original flowlines

        assert len(points) >= 2, "edges need to have at least two (end-)points"

        # FIXME: now, we allow jumping back, replacing the last edge
        # point with the node position.  For this, a distance of 0.25
        # could be too much - remove more points?

        # if flowlines do not end in a maximum (their end labels
        # are negative), we assign end nodes to them:
        if startNodeLabel <= 0:
            pos = points[0]
            nearestNode = map.nearestNode(pos, 0.25)
            # if there is already a node nearby and in the right
            # direction (prevents loops), attach edge to it:
            if nearestNode:
                diff = nearestNode.position() - pos
                # we are not interested in creating short, unsortable self-loops:
                if nearestNode.label() != endNodeLabel:
                    startNodeLabel = nearestNode.label()
                    if dot(diff, pos - points[1]) >= 0: # don't jump back
                        if diff.squaredMagnitude():
                            # include node position if not present
                            points.insert(0, nearestNode.position())
                    else:
                        points[0] = nearestNode.position()
            else: # no suitable Node found -> add one
                startNodeLabel = map.addNode(pos).label()

        # ..handle Edge end the same as the start:
        if endNodeLabel <= 0:
            pos = points[-1]
            nearestNode = map.nearestNode(pos, 0.25)
            if nearestNode:
                diff = nearestNode.position() - pos
                if nearestNode.label() != startNodeLabel:
                    endNodeLabel = nearestNode.label()
                    if dot(diff, pos - points[-2]) >= 0:
                        if diff.squaredMagnitude():
                            points.append(nearestNode.position())
                    else:
                        points[-1] = nearestNode.position()
            else:
                endNodeLabel = map.addNode(pos).label()

        if len(points) == 2 and points[0] == points[1]:
            # don't add self-loops with area zero
            result.append((startNodeLabel, endNodeLabel, points))
            continue
        
        if startNodeLabel > 0 and endNodeLabel > 0:
            map.addEdge(startNodeLabel, endNodeLabel, points, edgeLabel)
        else:
            result.append((startNodeLabel, endNodeLabel, points))

    return result

def connectBorderNodes(map, epsilon,
                       samePosEpsilon = 1e-6, aroundPixels = False):
    """connectBorderNodes(map, epsilon,
                       samePosEpsilon = 1e-6, aroundPixels = False)

    Inserts border edges around (see below for details) the image into
    `map`; all Nodes which are less than `epsilon` pixels away from
    that border are connected to it.  If the distance is larger than
    samePosEpsilon, an extra perpendicular edge is used for the
    connection, otherwise the border will run through the existing
    node.

    The optional parameter aroundPixels determines the position of the
    border: If it is set to False (default), the border will run
    through the pixel centers, i.e. from 0/0 to w-1/h-1 (where w,h =
    map.imageSize()).  Otherwise, the border will run around the pixel
    facets, i.e. be 0.5 larger in each direction."""
    
    dist = aroundPixels and 0.5 or 0.0
    x1, y1 = -dist, -dist
    x2, y2 = map.imageSize()[0] - 1 + dist, map.imageSize()[1] - 1 + dist

    left = []
    right = []
    top = []
    bottom = []
    for node in map.nodeIter():
        p = node.position()
        if   p[0] > x2 - epsilon:
            right.append(node)
        elif p[0] < x1 + epsilon:
            left.append(node)
        elif p[1] > y2 - epsilon:
            bottom.append(node)
        elif p[1] < y1 + epsilon:
            top.append(node)

    def XPosCompare(node1, node2):
        return cmp(node1.position()[0], node2.position()[0])

    def YPosCompare(node1, node2):
        return cmp(node1.position()[1], node2.position()[1])

    left.sort(YPosCompare); left.reverse()
    right.sort(YPosCompare)
    top.sort(XPosCompare)
    bottom.sort(XPosCompare); bottom.reverse()

    borderEdges = []
    lastNode = None

    lastPoints = [Vector2(x1, y1)]
    for node in top:
        thisPoints = []
        if node.position()[1] > y1 + samePosEpsilon:
            thisPoints.append(Vector2(node.position()[0], y1))
        thisPoints.append(node.position())
        lastPoints.extend(thisPoints)
        borderEdges.append((lastPoints, node))
        thisPoints.reverse()
        lastPoints = thisPoints
        lastNode = node

    lastPoints.append(Vector2(x2, y1))
    for node in right:
        thisPoints = []
        if node.position()[0] < x2 - samePosEpsilon:
            thisPoints.append(Vector2(x2, node.position()[1]))
        thisPoints.append(node.position())
        lastPoints.extend(thisPoints)
        borderEdges.append((lastPoints, node))
        thisPoints.reverse()
        lastPoints = thisPoints
        lastNode = node

    lastPoints.append(Vector2(x2, y2))
    for node in bottom:
        thisPoints = []
        if node.position()[1] < y2 - samePosEpsilon:
            thisPoints.append(Vector2(node.position()[0], y2))
        thisPoints.append(node.position())
        lastPoints.extend(thisPoints)
        borderEdges.append((lastPoints, node))
        thisPoints.reverse()
        lastPoints = thisPoints
        lastNode = node

    lastPoints.append(Vector2(x1, y2))
    for node in left:
        thisPoints = []
        if node.position()[0] > x1 + samePosEpsilon:
            thisPoints.append(Vector2(x1, node.position()[1]))
        thisPoints.append(node.position())
        lastPoints.extend(thisPoints)
        borderEdges.append((lastPoints, node))
        thisPoints.reverse()
        lastPoints = thisPoints
        lastNode = node

    if not borderEdges:
        return

    lastPoints.extend(borderEdges[0][0])
    borderEdges[0] = (lastPoints, borderEdges[0][1])

    from maputils import BORDER_PROTECTION

    endNodeLabel = lastNode.label()
    for points, node in borderEdges:
        startNodeLabel = endNodeLabel
        endNodeLabel = node.label()
        map.addEdge(startNodeLabel, endNodeLabel, points) \
                        .setFlag(BORDER_PROTECTION)

# --------------------------------------------------------------------
#                         consistency checks
# --------------------------------------------------------------------

class _CLCFaceLookup(object):
    def __init__(self, map):
        self._map = map
        self.errorCount = 0
        self.errorLabels = []
        self.pixelAreas = [0] * map.maxFaceLabel()

    def __call__(self, faceLabel):
        if faceLabel < 0:
            return
        faceLabel = int(faceLabel)
        self.pixelAreas[faceLabel] += 1
        try:
            assert self._map.face(faceLabel) != None
        except:
            self.errorCount += 1
            if not faceLabel in self.errorLabels:
                self.errorLabels.append(faceLabel)

def checkLabelConsistency(map):
    """Checks that no unknown positive/face labels occur in the
    labelimage, and that each face's pixelArea() is correct."""
    fl = _CLCFaceLookup(map)
    labelImage = map.labelImage()
    inspectImage(labelImage, fl)
    if fl.errorCount:
        sys.stderr.write("labelImage contains %d pixels with unknown faces!\n" % (
            fl.errorCount, ))
        sys.stderr.write("  unknown face labels found: %s\n" % (fl.errorLabels, ))
        if fl.errorCount < 40:
            for p in labelImage.size():
                if int(labelImage[p]) in fl.errorLabels:
                    print "   label %d at %s" % (int(labelImage[p]), p)
    result = (fl.errorCount == 0)
    for face in map.faceIter(skipInfinite = True):
        if face.pixelArea() != fl.pixelAreas[face.label()]:
            sys.stderr.write(
                "pixel area of Face %d is wrong (%d, should be %d)\n" %
                (face.label(), face.pixelArea(), fl.pixelAreas[face.label()]))
            result = False
    return result

# --------------------------------------------------------------------

def showMapStats(map):
    """showMapStats(map)
    Dumps number of cells and points to stdout (returns nothing.)"""
    pointCount = 0
    totalLength = 0.0
    for edge in map.edgeIter():
        pointCount += len(edge)
        totalLength += edge.length()

    if map.edgeCount == 0:
        print "empty map!"
        return

    print ("%d nodes, %d edges, and %d faces with a total of %d points\n  " + \
          "(mean p/edge: %.2f, mean dist.: %.2f, density: %.2f p/px)") % (
        map.nodeCount, map.edgeCount, map.faceCount, pointCount,
        float(pointCount) / map.edgeCount,
        totalLength / (pointCount - map.edgeCount),
        float(pointCount)/map.imageSize().area())
    
    if hasattr(map, "deleted") and map.deleted:
        print "%d edges were deleted (e.g. at image border)." % (
            len(map.deleted), )
    if hasattr(map, "unsortable") and map.unsortable:
        print "%d unsortable groups of edges occured." % (
            len(map.unsortable), )
    if hasattr(map, "unembeddableContours") and map.unembeddableContours:
        print "%d outer contours could not be embedded into " \
              "their surrounding faces!" % (len(map.unembeddableContours), )

def degree2Nodes(map):
    return [node for node in map.nodeIter() if node.degree() == 2]

# --------------------------------------------------------------------
#                    basic map cleanup operations
# --------------------------------------------------------------------

def removeCruft(map, what = 3, doChecks = False):
    """removeCruft(map, what = 3, doChecks = False)
    
    what is a bit-combination of
    1: for the removal of degree 0-nodes (default)
    2: removal of degree 2-nodes (default)
    4: removal of bridges
    8: removal of edges (i.e. all non-protected)

    if doChecks is True, consistency checks are performed after every
    operation.  As soon as that fails, removeCruft returns False.

    After normal operation, removeCruft returns the number of
    operations performed."""

    class OperationCounter(object):
        def __init__(self):
            self.count = 0

        def perform(self, op, dart):
            if op(dart):
                self.count += 1
            return True

    class CarefulCounter(OperationCounter):
        def perform(self, op, dart):
            OperationCounter.perform(self, op, dart)
            return checkConsistency(map)

    if doChecks:
        result = CarefulCounter()
    else:
        result = OperationCounter()

    if what & 8:
        for edge in map.edgeIter():
            if edge.leftFaceLabel() != edge.rightFaceLabel():
                if not result.perform(map.mergeFaces, edge.dart()):
                    return False

    if what & 4:
        for edge in map.edgeIter():
            if edge.leftFaceLabel() == edge.rightFaceLabel():
                if not result.perform(map.removeBridge, edge.dart()):
                    return False

    if what & 2:
        for node in map.nodeIter():
            if node.degree() == 2 and \
                   (node.anchor().endNode() != node):
                if not result.perform(map.mergeEdges, node.anchor()):
                    return False

    if what & 1:
        for node in map.nodeIter():
            if node.degree() == 0:
                if not result.perform(map.removeIsolatedNode, node):
                    return False

    print "removeCruft(): %d operations performed." % result.count
    return result.count

def removeSmallRegions(map, minArea):
    result = 0
    for face in map.faceIter(skipInfinite = True):
        if face.area() < minArea:
            po = list(face.contour().phiOrbit())
            if len(po) > 1:
                sys.stderr.write("WARNING: removeSmallRegions() has to decide about neighbor\n  which %s is to be merged into!\n" % face)
            for dart in po:
                if mergeFacesCompletely(dart):
                    result += 1
                    break
    return result

# --------------------------------------------------------------------
#                             simple proxies
# --------------------------------------------------------------------

def removeIsolatedNode(node):
    return node.anchor().map().removeIsolatedNode(node)

def mergeEdges(dart):
    return dart.map().mergeEdges(dart)

def removeBridge(dart):
    return dart.map().removeBridge(dart)

def mergeFaces(dart):
    return dart.map().mergeFaces(dart)

# --------------------------------------------------------------------
#                       composed Euler operations
# --------------------------------------------------------------------

def removeEdge(dart):
    """removeEdge(dart)

    Composed Euler operation which calls either removeBridge or
    mergeFaces, depending on whether the dart belongs to an edge or a
    bridge.

    Returns the surviving face."""

    map = dart.map()
    if dart.leftFaceLabel() == dart.rightFaceLabel():
        return map.removeBridge(dart)
    else:
        return map.mergeFaces(dart)

def mergeFacesCompletely(dart, doRemoveDegree2Nodes = True):
    """mergeFacesCompletely(dart, doRemoveDegree2Nodes = True)

    In contrast to the Euler operation mergeFaces(), this function
    removes all common edges of the two faces, not only the single
    edge belonging to dart.

    Furthermore, if the optional parameter doRemoveDegree2Nodes is
    True (default), all nodes whose degree is reduced to two will be
    merged into their surrounding edges.

    Returns the surviving face."""
    
    #print "mergeFacesCompletely(%s, %s)" % (dart, doRemoveDegree2Nodes)
    if dart.edge().isBridge():
        raise TypeError("mergeFacesCompletely(): dart belongs to a bridge!")
    map = dart.map()
    rightLabel = dart.rightFaceLabel()
    commonEdgeList = []
    for contourIt in dart.phiOrbit():
        if contourIt.rightFaceLabel() == rightLabel:
            if contourIt.edge().flags():
                return None
            commonEdgeList.append(contourIt)

    assert commonEdgeList, "mergeFacesCompletely(): no common edges found!"
    affectedNodes = []

    survivor = None
    for dart in commonEdgeList:
        affectedNodes.append(dart.startNodeLabel())
        affectedNodes.append(dart.endNodeLabel())
        assert dart.edge().flags() == 0
        if survivor == None:
            survivor = map.mergeFaces(dart) # first common edge
        else:
            assert survivor == map.removeBridge(dart)

    for nodeLabel in affectedNodes:
        node = map.node(nodeLabel)
        if not node: continue
        if node.degree == 0:
            map.removeIsolatedNode(node)
        if doRemoveDegree2Nodes and node.degree() == 2:
            d = node.anchor()
            if d.endNodeLabel() != node.label():
                map.mergeEdges(d)

    return survivor

def mergeFacesByLabel(map, label1, label2, doRemoveDegree2Nodes = True):
    """mergeFacesByLabel(map, label1, label2, doRemoveDegree2Nodes = True)

    Similar to mergeFacesCompletely() (which is called to perform the
    action), but is parametrized with two face labels and finds a
    common dart of these labels.

    Returns the surviving face (or None if no common edge was found)."""
    
    face1 = map.face(label1)
    if not face1:
        sys.stderr.write("mergeFacesByLabel: face with label1 = %d does not exist!\n"
                         % (label1, ))
        return
    if not map.face(label2):
        sys.stderr.write("mergeFacesByLabel: face with label2 = %d does not exist!\n"
                         % (label2, ))
        return
    for dart in face1.contours():
        for contourIt in dart.phiOrbit():
            if contourIt.rightFaceLabel() == label2:
                return mergeFacesCompletely(contourIt, doRemoveDegree2Nodes)

# --------------------------------------------------------------------

def thresholdMergeCost(map, mergeCostFunctor, maxCost, costs = None, q = None):
    """thresholdMergeCost(map, mergeCostFunctor, maxCost, costs = None, q = None)

    Merges faces of the given map in order of increasing costs until
    no more merges are assigned a cost <= maxCost by the
    mergeCostFunctor.

    The mergeCostFunctor is called on a dart for each edge to
    determine the cost of removing that edge.  This is used to
    initialize a DynamicCostQueue, which is used internally in order
    to always remove the edge with the lowest cost assigned.  The
    function returns a pair of the number of operations performed and
    the DynamicCostQueue, and you may pass the latter as optional
    argument 'd' into a subsequent call of thresholdMergeCost (with
    the same map and an increased maxCost) in order to re-use it
    (mergeCostFunctor is not used then).

    If the optional argument costs is given, it should be a mutable
    sequence which is append()ed the costs of each performed
    operation."""
    
    result = 0

    if q == None:
        q = hourglass.DynamicCostQueue(map.maxEdgeLabel()+1)
        for edge in map.edgeIter():
            q.insert(edge.label(), mergeCostFunctor(edge.dart()))
        
    while not q.empty():
        edgeLabel, cost = q.pop()
        if cost > maxCost:
            break

        edge = map.edge(edgeLabel)
        if not edge or edge.flags():
            continue
        d = edge.dart()
        if edge.isBridge():
            survivor = removeBridge(d)
        else:
            survivor = mergeFacesCompletely(d)
            if survivor:
                for anchor in survivor.contours():
                    for dart in anchor.phiOrbit():
                        q.setCost(dart.edgeLabel(),
                                  mergeCostFunctor(dart))
        if survivor:
            result += 1
            if costs != None:
                costs.append(cost)
    
    return result, q

# --------------------------------------------------------------------
#                   topological utility functions
# --------------------------------------------------------------------

def neighbors(face, unique = True):
    """neighbors(face, unique = True) -> list

    Returns a list of adjacent faces, by default without duplicates
    (in case of multiple common edges)."""
    
    result = []
    for anchor in face.contours():
        for dart in anchor.phiOrbit():
            result.append(dart.rightFace())
    if unique:
        result = dict.fromkeys(result).keys()
    return result

def holeComponent(dart, includeExterior = False):
    """holeComponent(dart, includeExterior = False) -> list

    Returns list of all faces of the connected combinatorial map of
    the given dart.  If includeExterior is True, the first element of
    the result is dart.leftFace() (which is expected to be the face
    the above map is embedded in).  By default, this face is not
    returned.

    This is supposed to be called with e.g. the inner (hole) anchor
    darts of a face."""

    result = [dart.leftFace()]
    seen   = {dart.leftFaceLabel() : None}
    border = dict.fromkeys([d.rightFace() for d in dart.phiOrbit()])
    while border:
        face = border.popitem()[0]
        if seen.has_key(face.label()):
            continue
        result.append(face)
        seen[face.label()] = None
        for dart in face.contour().phiOrbit():
            border[dart.rightFace()] = None
    if not includeExterior:
        del result[0]
    return result

def showHomotopyTree(face, indentation = ""):
    """showHomotopyTree(root)

    Prints homotopy tree to stdout, starting with a given face
    as root.  You can also pass a GeoMap as parameter, in which case
    the tree will start with its infinite face."""
    
    if not hasattr(face, "label"):
        face = face.face(0)
    print indentation + str(face)
    for anchor in face.holeContours():
        for hole in holeComponent(anchor):
            showHomotopyTree(hole, indentation + "  ")
