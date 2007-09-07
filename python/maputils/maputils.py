import vigra, hourglass, sys, math, time, weakref

import flag_constants, progress, sivtools

# --------------------------------------------------------------------
#                            edge protection
# --------------------------------------------------------------------

class EdgeProtection(object):
    """Protects GeoMap Edges which have a protection flag set.
    I.e. all operations that would remove an edge for which
    edge.flag(flag_constants.ALL_PROTECTION) is not 0 will be
    canceled automatically."""
    
    def __init__(self, map):
        self._attach(map)
        # prevent cycles if this is an attribute of the map:
        self._map = weakref.ref(map) # only needed for pickle support

    def _attach(self, map):
        assert not hasattr(self, "_attachedHooks"), \
               "trying to attach to more than one GeoMap?!"
        self._attachedHooks = (
            map.addMergeFacesCallbacks(self.preRemoveEdge, None),
            map.addRemoveBridgeCallbacks(self.preRemoveEdge, None),
            map.addMergeEdgesCallbacks(self.preMergeEdges, None))

    def detachHooks(self):
        """Detach this objects from its GeoMaps' hooks.  Note that if
        you do not call this method, the GeoMap keeps references to
        this object, and it will be impossible to destroy it without
        deleting the GeoMap, too."""
        for cb in self._attachedHooks:
            cb.disconnect()
        self._attachedHooks = ()

    def preRemoveEdge(self, dart):
        "do not allow removal of protected edges"
        return not dart.edge().flag(flag_constants.ALL_PROTECTION)

    def preMergeEdges(self, dart):
        "only allow edge merging if the edges carry the same flags"
        return (dart.edge().flags() ==
                dart.clone().nextSigma().edge().flags())

    def __getstate__(self):
        return (self._map(), )

    def __setstate__(self, (map, )): # cf. __init__
        self._attach(map)
        self._map = weakref.ref(map)

    def __repr__(self):
        map = ", no map"

        if not hasattr(self, "_attachedHooks"):
            hooks = "not attached"
            map = ""
        elif not self._attachedHooks:
            hooks = "no hooks"
        else:
            active = self._attachedHooks[0].connected() and "active" or "detached"
            hooks = "%d %s hooks" % (len(self._attachedHooks), active)

        if self._map():
            addr = id(self._map()) + 0x100000000L
            map = " for GeoMap @0x%8x" % addr

        return "<%s, %s%s>" % (self.__class__.__name__, hooks, map)

def protectFace(face, protect = True, flag = flag_constants.PROTECTED_FACE):
    """Sets the PROTECTED_FACE flag of 'face' according to 'protect'.
    Subsequently, sets the CONTOUR_PROTECTION of each edge in the
    contours of 'face' iff either of the adjacent faces is
    protected."""
    
    face.setFlag(flag, protect)
    for dart in contourDarts(face):
        dart.edge().setFlag(flag_constants.CONTOUR_PROTECTION,
                            protect or dart.rightFace().flag(flag))

# --------------------------------------------------------------------
#                      subpixel watershed functions
# --------------------------------------------------------------------

class PassValueFilter(object):
    def __init__(self, biSIV, threshold):
        self._biSIV = biSIV
        self._threshold = threshold

    def __call__(self, position):
        return self._biSIV[position] >= self._threshold

    def __str__(self):
        return "pass value >= %s" % self._threshold

class SaddleTensorFeature(object):
    def __init__(self, gmSIV, stSIV):
        self._hessian = sivtools.HessianSIVProxy(gmSIV)
        self._stSIV = stSIV

    def saddleDir(self, position):
        """Return tangent direction of flowline starting at the given
        saddle position.  (Direction of larger eigenvector.)"""
        gxx, gxy, gyy = self._hessian[position]
        return 0.5 * math.atan2(-2.0*gxy, gxx-gyy)

    def _orthoGrad2(self, position, onlyDir = False):
        # tangent angle of flowline:
        ta_fl = self.saddleDir(position)
        c = math.cos(ta_fl)
        s = math.sin(ta_fl)

        # component of structure tensor in normal direction:
        s11, s12, s22 = self._stSIV[position]
        og2 = s11*vigra.sq(s) - 2*s12*c*s + s22*vigra.sq(c)

        if onlyDir:
            # large eigenvalue of structure tensor:
            ev = 0.5 * (s11 + s22 + math.sqrt(vigra.sq(s11 - s22)
                                              + 4.0*vigra.sq(s12)))
            return og2 / ev

        return og2

    def orthogonalGradient(self, position):
        return math.sqrt(self._orthoGrad2(position))

    def directionMatch(self, position):
        return self._orthoGrad2(position, True)

class SaddleOrthogonalGradientFilter(SaddleTensorFeature):
    def __init__(self, gmSIV, stSIV, threshold):
        SaddleTensorFeature.__init__(self, gmSIV, stSIV)
        self._threshold = threshold

    def __call__(self, position):
        return math.sqrt(self._orthoGrad2(position)) >= self._threshold

    def __str__(self):
        return "orthogonal gradient >= %s" % self._threshold

class SaddleDirectionMatchFilter(SaddleTensorFeature):
    def __init__(self, gmSIV, stSIV, threshold):
        SaddleTensorFeature.__init__(self, gmSIV, stSIV)
        self._threshold = threshold

    def __call__(self, position):
        return self._orthoGrad2(position, True) >= self._threshold

    def __str__(self):
        return "direction match >= %s" % self._threshold

def filterSaddlePoints(rawSaddles, biSIV, filter, maxDist):
    """Filter saddle points first by checking the passValue in the
    boundary indicator SplineImageView 'biSIV', then by removing
    duplicates (points which are nearer to previous points than
    maxDist - IOW: Points that come first win.)

    Return a list of the remaining points together with their indices
    in the original array (in enumerate() fashion)."""

    maxSquaredDist = math.sq(maxDist)
    result = []
    sorted = []
    for k, saddle in enumerate(rawSaddles):
        if k == 0:
            assert not saddle, "rawSaddles[0] expected to be None"
            continue
        if filter and not filter(saddle):
            continue
        sorted.append((-biSIV[saddle], k, saddle))

    sorted.sort()

    knownSaddles = hourglass.PositionedMap()
    for _, k, saddle in sorted:
        if not knownSaddles(saddle, maxSquaredDist):
            result.append((k, saddle))
            knownSaddles.insert(saddle, saddle)

    return result

def subpixelWatershedData(spws, biSIV, filter = None, mask = None,
                          minSaddleDist = 0.1, # sensible: ssMinDist
                          perpendicularDistEpsilon = 0.1, maxStep = 0.1):
    """Calculates and returns a pair with a list of maxima and a list of
    edges (composed flowline pairs) from the SubPixelWatersheds object
    'spws'.  Each edge is represented with a quadrupel of
    startNodeIndex, endNodeIndex, edge polygon and the index of the
    original saddle within that polygon.  The first edge is None.
    That output is suitable and intended for addFlowLinesToMap().

    Gives verbose output during operation and uses filterSaddlePoints
    to filter out saddle points using the given filter criterion and
    to filter out duplicate saddlepoints (pass the optional argument
    'minSaddleDist' to change the default of 0.1 here).

    Each found edge polygon is simplified using simplifyPolygon with
    perpendicularDistEpsilon = 0.1 and maxStep = 0.1 (use the optional
    parameters with the same names to change the default)."""
    
    sys.stdout.write("- finding critical points..")
    c = time.clock()

    if mask:
        spws.findCriticalPoints(mask)

    rawSaddles = spws.saddles()
    maxima     = spws.maxima()
    sys.stdout.write("done. (%ss, %d maxima, %d saddles, %d minima)\n" % (
        time.clock()-c,
        len(maxima)-1, len(rawSaddles)-1, len(spws.minima())-1))

    def calculateEdge(index):
        flowline = spws.edge(index)
        if not flowline:
            return flowline
        sn, en, poly, saddleIndex = flowline
        if perpendicularDistEpsilon:
            simple = hourglass.simplifyPolygon(
                poly[:saddleIndex+1], perpendicularDistEpsilon, maxStep)
            newSaddleIndex = len(simple)-1
            simple.extend(hourglass.simplifyPolygon(
                poly[saddleIndex:], perpendicularDistEpsilon, maxStep))
            poly = simple
            saddleIndex = newSaddleIndex
        return (sn, en, poly, saddleIndex, index)

    saddles = filterSaddlePoints(rawSaddles, biSIV, filter, minSaddleDist)
    prefix = "- following %d/%d edges.." % (len(saddles), len(rawSaddles)-1)
    if filter:
        prefix += " (%s)" % (filter, )
    else:
        prefix += " (no saddle threshold)"

    c = time.clock()
    flowlines = [None]
    percentGranularity = len(saddles) / 260 + 1
    for k, _ in saddles:
        edgeTuple = calculateEdge(k)
        if edgeTuple:
            flowlines.append(edgeTuple)
        if k % percentGranularity == 0:
            sys.stderr.write("%s %d%%\r" % (
                prefix, 100 * (len(flowlines)-1) / len(saddles), ))
    print "%s done (%ss)." % (prefix, time.clock()-c)

    if len(flowlines)-1 < len(saddles):
        print "  (%d edges parallel to border removed)" % (
            len(saddles) - (len(flowlines)-1))

    if perpendicularDistEpsilon:
        print "  (simplified edges with perpendicularDistEpsilon = %s, maxStep = %s)" % (
            perpendicularDistEpsilon, maxStep)

    return maxima, flowlines

def _handleUnsortable(map, unsortable):
    # remove unsortable self-loops (most likely flowlines which
    # did not get far and were connected to the startNode by
    # addFlowLinesToMap())
    for group in unsortable:
        i = 0
        while i < len(group):
            dart = map.dart(group[i])
            if dart.edge().isLoop() and -dart.label() in group:
                group.remove(dart.label())
                group.remove(-dart.label())
            else:
                i += 1
    try:
        while True:
            unsortable.remove([])
    except ValueError:
        pass
    assert not unsortable, "unhandled unsortable edges occured"

def subpixelWatershedMapFromData(
    maxima, flowlines, imageSize,
    borderConnectionDist = 0.1,
    ssStepDist = 0.2, ssMinDist = 0.12,
    performBorderClosing = True,
    performEdgeSplits = True,
    wsStatsSpline = None,
    minima = None,
    Map = hourglass.GeoMap):
    """`performEdgeSplits` may be a callable that is called before
    splitting edges, with the complete locals() dict as parameter
    (useful keys are 'spmap' and all parameters of this function)."""

    print "- initializing GeoMap from flowlines..."
    spmap = Map(maxima, [], imageSize)

    deleted = addFlowLinesToMap(flowlines, spmap)
    if deleted:
        print "  skipped %d broken flowlines." % len(deleted)

    if performBorderClosing:
        print "  adding border edges and EdgeProtection..."
        connectBorderNodes(spmap, borderConnectionDist)
        spmap.edgeProtection = EdgeProtection(spmap)

    removeIsolatedNodes(spmap)
    unsortable = spmap.sortEdgesEventually(
        ssStepDist, ssMinDist, bool(performEdgeSplits))
    _handleUnsortable(spmap, unsortable)

    if wsStatsSpline:
        p = progress.StatusMessage("  initializing watershed statistics")
        from statistics import WatershedStatistics
        spmap.wsStats = WatershedStatistics(
            spmap, flowlines, wsStatsSpline)
        p.finish()

    if performEdgeSplits:
        if not isinstance(performEdgeSplits, int):
            performEdgeSplits(locals())
        p = progress.StatusMessage("  splitting & joining parallel edges")
        spmap.splitParallelEdges()
        p.finish()

    spmap.initializeMap()

    if minima:
        assert wsStatsSpline, \
            "minima given, but basin statistics need a wsStatsSpline, too"
        p = progress.StatusMessage("  initializing watershed basin statistics")
        from statistics import WatershedBasinStatistics
        spmap.wsBasinStats = WatershedBasinStatistics(
            spmap, minima[1:], wsStatsSpline)
        p.finish()

    return spmap

def subpixelWatershedMap(
    boundaryIndicator, splineOrder = 5,
    saddleThreshold = None, mask = None,
    saddleOrthoGrad = None, saddleDirMatch = None, biTensor = None,
    perpendicularDistEpsilon = 0.1, maxStep = 0.1,
    borderConnectionDist = 0.1,
    ssStepDist = 0.2, ssMinDist = 0.12,
    performBorderClosing = True,
    performEdgeSplits = True,
    initWSStats = True,
    Map = hourglass.GeoMap):

    """`boundaryIndicator` should be an image with e.g. a gradient
    magnitude.
    
    If `mask` is False, all pixels are searched for critical
    points.  By default (mask == None), pixels below saddleThreshold/2
    are skipped.

    If `initWSStats` is True, the resulting GeoMap will have an
    attribute 'wsStats' with a `statistics.WatershedStatistics`
    instance."""

    SPWS = getattr(hourglass, "SubPixelWatersheds%d" % splineOrder)
    spws = SPWS(boundaryIndicator)

    if hasattr(boundaryIndicator, "siv"):
        siv = boundaryIndicator.siv
    else:
        SIV = sivtools.sivByOrder(splineOrder)
        siv = SIV(boundaryIndicator)

    if mask == None and saddleThreshold or saddleOrthoGrad:
        threshold = saddleThreshold or saddleOrthoGrad
        mask = vigra.transformImage(
            boundaryIndicator, "\l x: x > %s ? 1 : 0" % (threshold/2, ))

    filter = None
    if saddleThreshold:
        filter = PassValueFilter(siv, saddleThreshold)
    elif saddleOrthoGrad or saddleDirMatch:
        assert biTensor, "for saddle filtering using SOG/SDM, biTensor is needed!"
        if not hasattr(biTensor, "siv"):
            biTensor.siv = sivtools.TensorSIVProxy(biTensor)
        if saddleOrthoGrad:
            filter = SaddleOrthogonalGradientFilter(siv, biTensor.siv, saddleOrthoGrad)
        else:
            filter = SaddleDirectionMatchFilter(siv, biTensor.siv, saddleDirMatch)

    maxima, flowlines = subpixelWatershedData(
        spws, siv, filter, mask,
        minSaddleDist = ssMinDist,
        perpendicularDistEpsilon = perpendicularDistEpsilon, maxStep = maxStep)
    
    spwsMap = subpixelWatershedMapFromData(
        maxima, flowlines, boundaryIndicator.size(),
        borderConnectionDist = borderConnectionDist,
        ssStepDist = ssStepDist, ssMinDist = ssMinDist,
        performBorderClosing = performBorderClosing,
        performEdgeSplits = performEdgeSplits,
        wsStatsSpline = initWSStats and siv or None,
        minima = initWSStats and spws.minima() or None,
        Map = Map)

    return spwsMap

def addFlowLinesToMap(edges, map):
    """addFlowLinesToMap(edges, map)

    This function expects `edges` to be a list of
    SubPixelWatersheds.edge() return values and adds edges for each
    flowline.  It contains some special handling of flowlines:

    * Additional Nodes may be added for flowlines that did not end in
      a maximum - extra care is taken not to insert multiple Nodes at
      (nearly) the same position.

    * Self-loops with area zero are not added.

    Returns edgeTuples that could not be added to the GeoMap."""
    
    # Node 0 conflicts with our special handling of 0 values:
    assert not map.node(0), \
           "addFlowLinesToMap: Node with label zero should not exist!"

    result = []
    for edgeLabel, edgeTuple in enumerate(edges):
        if not edgeTuple:
            continue

        startNodeLabel = edgeTuple[0]
        endNodeLabel = edgeTuple[1]
        if startNodeLabel == -2 and endNodeLabel == -2:
            result.append(edgeTuple)
            continue # unwanted edge parallel to border

        # be careful not to modify the original 'edges' passed:
        points = hourglass.Polygon(edgeTuple[2])

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
                startNode = nearestNode
                if vigra.dot(diff, pos - points[1]) >= 0: # don't jump back
                    if diff.squaredMagnitude():
                        # include node position if not present
                        points.insert(0, nearestNode.position())
                else:
                    points[0] = nearestNode.position()
            else: # no suitable Node found -> add one
                startNode = map.addNode(pos)
        else:
            startNode = map.node(startNodeLabel)
            assert startNode, "invalid startNodeLabel!"

        # ..handle Edge end the same as the start:
        if endNodeLabel <= 0:
            pos = points[-1]
            nearestNode = map.nearestNode(pos, 0.25)
            if nearestNode:
                diff = nearestNode.position() - pos
                endNode = nearestNode
                if vigra.dot(diff, pos - points[-2]) >= 0:
                    if diff.squaredMagnitude():
                        points.append(nearestNode.position())
                else:
                    points[-1] = nearestNode.position()
            else:
                endNode = map.addNode(pos)
        else:
            endNode = map.node(endNodeLabel)
            assert endNode, "invalid endNodeLabel!"

        # we need to avoid creating short, unsortable
        # self-loops with area zero:
        if startNode == endNode and (
            startNodeLabel <= 0 or endNodeLabel <= 0 or
            len(points) == 2):
            result.append(edgeTuple)
            continue
        
        assert startNode and endNode
        map.addEdge(startNode, endNode, points, edgeLabel)

    return result

# --------------------------------------------------------------------
#                        map creation helpers
# --------------------------------------------------------------------

def mapFromEdges(edges, imageSize, GeoMap = hourglass.GeoMap):
    # FIXME: comment
    
    def getNode(map, position):
        node = map.nearestNode(position, 0.001)
        if not node:
            node = map.addNode(position)
        return node
    
    result = GeoMap([], [], imageSize)
    for edge in edges:
        sn = getNode(result, edge[0])
        en = getNode(result, edge[-1])
        result.addEdge(sn, en, edge)
    
    return result

def gridMap(gridSize = (10, 10), firstPos = vigra.Vector2(0.5, 0.5),
            dist = vigra.Vector2(1, 1), imageSize = None):
    """Create a GeoMap representing a rectangular grid."""
    
    xDist = vigra.Vector2(dist[0], 0)
    yDist = vigra.Vector2(0, dist[1])
    if not imageSize:
        imageSize = (int(math.ceil(gridSize[0] * dist[0] + 2*(firstPos[0]+0.5))),
                     int(math.ceil(gridSize[1] * dist[1] + 2*(firstPos[1]+0.5))))

    map = hourglass.GeoMap(imageSize)

    def addEdge(n1, n2):
        map.addEdge(n1, n2, [n1.position(), n2.position()])

    prevRow = None
    for y in range(gridSize[1]+1):
        row = []
        for x in range(gridSize[0]+1):
            pos = firstPos + x * xDist + y * yDist
            row.append(map.addNode(pos))

        for x in range(gridSize[0]):
            addEdge(row[x], row[x+1])

        if prevRow:
            for x in range(gridSize[0]+1):
                addEdge(row[x], prevRow[x])

        prevRow = row

    return map

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

    lastPoints = [vigra.Vector2(x1, y1)]
    for node in top:
        thisPoints = []
        if node.position()[1] > y1 + samePosEpsilon:
            thisPoints.append(vigra.Vector2(node.position()[0], y1))
        thisPoints.append(node.position())
        lastPoints.extend(thisPoints)
        borderEdges.append((lastPoints, node))
        thisPoints.reverse()
        lastPoints = thisPoints
        lastNode = node

    lastPoints.append(vigra.Vector2(x2, y1))
    for node in right:
        thisPoints = []
        if node.position()[0] < x2 - samePosEpsilon:
            thisPoints.append(vigra.Vector2(x2, node.position()[1]))
        thisPoints.append(node.position())
        lastPoints.extend(thisPoints)
        borderEdges.append((lastPoints, node))
        thisPoints.reverse()
        lastPoints = thisPoints
        lastNode = node

    lastPoints.append(vigra.Vector2(x2, y2))
    for node in bottom:
        thisPoints = []
        if node.position()[1] < y2 - samePosEpsilon:
            thisPoints.append(vigra.Vector2(node.position()[0], y2))
        thisPoints.append(node.position())
        lastPoints.extend(thisPoints)
        borderEdges.append((lastPoints, node))
        thisPoints.reverse()
        lastPoints = thisPoints
        lastNode = node

    lastPoints.append(vigra.Vector2(x1, y2))
    for node in left:
        thisPoints = []
        if node.position()[0] > x1 + samePosEpsilon:
            thisPoints.append(vigra.Vector2(x1, node.position()[1]))
        thisPoints.append(node.position())
        lastPoints.extend(thisPoints)
        borderEdges.append((lastPoints, node))
        thisPoints.reverse()
        lastPoints = thisPoints
        lastNode = node

    if not borderEdges:
        cornerNode = map.addNode(lastPoints[0])
        lastPoints.append(lastPoints[0]) # close loop
        map.addEdge(cornerNode, cornerNode, lastPoints) \
                        .setFlag(flag_constants.BORDER_PROTECTION)
        return

    lastPoints.extend(borderEdges[0][0])
    borderEdges[0] = (lastPoints, borderEdges[0][1])

    endNode = lastNode
    for points, node in borderEdges:
        startNode = endNode
        endNode = node
        map.addEdge(startNode, endNode, points) \
                        .setFlag(flag_constants.BORDER_PROTECTION)

def copyMapContents(sourceMap, destMap = None, edgeTransform = None):
    """Add all nodes and edges of sourceMap to the destMap.  The sigma
    order is preserved.  Thus, it is an error to copy the contents of
    a graph - not sourceMap.edgesSorted() - into a map with
    destMap.edgesSorted().

    You can pass an optional edgeTransform to map edges to other
    Polygons: edgeTransform may be a callable that is called with Edge
    instances and must return an object suitable as the geometry
    parameter of GeoMap.addEdge.  If None is returned, the edge is
    skipped.

    Returns a tuple (destMap, nodes, edges), where nodes and edges are
    lists that map source nodes/edges onto their new counterparts in
    destMap.

    If destMap is None, a new GeoMap of the same size is created and
    returned (but not initialized!)."""

    if destMap == None:
        destMap = sourceMap.__class__(sourceMap.imageSize())
        if sourceMap.edgesSorted():
            # mark as sorted (sigma order will be copied from sourceMap):
            destMap.sortEdgesDirectly()

    assert sourceMap.edgesSorted() or not destMap.edgesSorted(), \
           "refraining from inserting unsorted edges into sorted map!"
    
    nodes = [None] * sourceMap.maxNodeLabel()
    # for better edgeTransform support, nodes are added on-the-fly now
    
    edges = [None] * sourceMap.maxEdgeLabel()
    for edge in sourceMap.edgeIter():
        geometry = edge
        if edgeTransform:
            geometry = edgeTransform(geometry)
            if geometry is None:
                continue

        startNeighbor = nodes[edge.startNodeLabel()]
        if startNeighbor == None:
            startNeighbor = destMap.addNode(geometry[0])
            nodes[edge.startNodeLabel()] = startNeighbor
        elif not startNeighbor.isIsolated():
            if edgeTransform:
                assert geometry[0] == startNeighbor.position(), "copyMapContents: edgeTransform leads to inconsistent node positions!"
            
            neighbor = edge.dart()
            while neighbor.nextSigma().edgeLabel() > edge.label() \
                      or neighbor.label() == -edge.label():
                pass
            assert neighbor.edgeLabel() < edge.label(), \
                   "since !startNeighbor.isIsolated(), there should be an (already handled) edge with a smaller label in this orbit!"
            startNeighbor = edges[neighbor.edgeLabel()].dart()
            if neighbor.label() < 0:
                startNeighbor.nextAlpha()

        endNeighbor = nodes[edge.endNodeLabel()]
        if endNeighbor == None:
            endNeighbor = destMap.addNode(geometry[-1])
            nodes[edge.endNodeLabel()] = endNeighbor
        elif not endNeighbor.isIsolated():
            if edgeTransform:
                assert geometry[-1] == endNeighbor.position(), "copyMapContents: edgeTransform leads to inconsistent node positions!"
            
            neighbor = edge.dart().nextAlpha()
            while neighbor.nextSigma().edgeLabel() > edge.label() \
                      or neighbor.label() == edge.label():
                pass
            assert neighbor.edgeLabel() < edge.label(), \
                   "since !endNeighbor.isIsolated(), there should be an (already handled) edge with a smaller label in this orbit!"
            endNeighbor = edges[neighbor.edgeLabel()].dart()
            if neighbor.label() < 0:
                endNeighbor.nextAlpha()

        newEdge = destMap.addEdge(startNeighbor, endNeighbor, geometry)
        newEdge.setFlag(edge.flags()) # retain Edge flags
        edges[edge.label()] = newEdge

    # now add isolated nodes:
    for node in sourceMap.nodeIter():
        if not nodes[node.label()]:
            nodes[node.label()] = destMap.addNode(node.position())

    return destMap, nodes, edges

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
    vigra.inspectImage(labelImage, fl)
    if fl.errorCount:
        sys.stderr.write("labelImage contains %d pixels with unknown faces!\n" % (
            fl.errorCount, ))
        sys.stderr.write("  unknown face labels found: %s\n" % (fl.errorLabels, ))
        if fl.errorCount < 40:
            for p in vigra.meshIter(labelImage.size()):
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

def drawLabelImage(aMap, scale = 1, verbose = True):
    """Return freshly drawn label image.  Should be identical to
    aMap.labelImage() if aMap.hasLabelImage() and scale == 1.  scale
    may be used to draw a label image with a different resolution;
    e.g. scale = 2 means that the returned image is (2w x 2h)
    where w,h = aMap.imageSize()."""

    total = aMap.faceCount - 1
    done = 1
    result = vigra.GrayImage(aMap.imageSize()*scale)
    holes = list(aMap.face(0).holeContours())
    # shift sampling points from middle of pixel (0.5, 0.5)
    # to middle of new, scaled pixel (scale/2, scale/2):
    offset = vigra.Vector2(scale / 2.0 - 0.5, scale / 2.0 - 0.5)
    for contour in holes:
        for hole in holeComponent(contour):
            if verbose and done % 23 == 0:
                sys.stdout.write("\r[%d%%] Face %d/%d" % (
                    done*100/total, done, total))
                sys.stdout.flush()
            poly = hourglass.Polygon(
                hourglass.contourPoly(hole.contour()) * scale + offset)
            sl = hourglass.scanPoly(poly, result.height())
            hourglass.fillScannedPoly(sl, result, hole.label())
            holes.extend(hole.holeContours())
            done += 1
    return result

def checkLabelConsistencyThoroughly(aMap):
    """Call `checkLabelConsistency` and additionally check that the
    labels are at the correct position (using `drawLabelImage`)."""
    
    class AssertRightLabel(object):
        def __init__(self):
            self.correct = True
        
        def __call__(self, label, shouldBe):
            if label >= 0:
                self.correct = self.correct and (label == shouldBe)

    return checkLabelConsistency(aMap) and \
           vigra.inspectImage(aMap.labelImage(),
                              drawLabelImage(aMap, verbose = False),
                              AssertRightLabel()).correct

def checkCachedPropertyConsistency(aMap):
    """Check whether edge and face bounds and areas are valid."""
    result = True
    realPolys = mapValidEdges(lambda edge: hourglass.Polygon(list(edge)), aMap)
    for edge in aMap.edgeIter():
        poly = realPolys[edge.label()]
        if abs(poly.length() - edge.length()) > 1e-6:
            sys.stderr.write("Edge %d has cached length %s instead of %s!\n" % (
                edge.label(), edge.length(), poly.length()))
            result = False
        if poly.boundingBox() != edge.boundingBox():
            sys.stderr.write("Edge %d has cached boundingBox %s instead of %s!\n" % (
                edge.label(), edge.boundingBox(), poly.boundingBox()))
            result = False
        if abs(poly.partialArea() - edge.partialArea()) > 1e-5:
            sys.stderr.write("Edge %d has cached partialArea %s instead of %s!\n" % (
                edge.label(), edge.partialArea(), poly.partialArea()))
            result = False
    for face in aMap.faceIter():
        bbox = hourglass.BoundingBox()
        area = 0.0
        for dart in face.contour().phiOrbit():
            edge = realPolys[dart.edgeLabel()]
            bbox |= edge.boundingBox()
            if dart.edge().isBridge():
                continue
            if dart.label() > 0:
                area += edge.partialArea()
            else:
                area -= edge.partialArea()
        
        if face.label() and bbox != face.boundingBox():
            sys.stderr.write("Face %d has cached bounding box %s instead of %s!\n" % (
                face.label(), face.boundingBox(), bbox))
            result = False
        if abs(area - face.area()) > 1e-5:
            sys.stderr.write("Face %d has cached area %s instead of %s!\n" % (
                face.label(), face.area(), area))
            result = False
    return result

def checkAllConsistency(aMap):
    """Return whether aMap.checkConsistency(),
    checkCachedPropertyConsistency(aMap), and
    checkLabelConsistencyThoroughly(aMap) succeed (the latter in turn
    calls checkLabelConsistency(aMap)."""
    return aMap.checkConsistency() and \
           checkCachedPropertyConsistency(aMap) and \
           (not aMap.hasLabelImage() or checkLabelConsistencyThoroughly(aMap))

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

def removeIsolatedNodes(map):
    """removeIsolatedNodes(map)

    Removes all isolated nodes with map.removeIsolatedNode(...) and
    returns the number of successful operations (= nodes removed)."""
    
    result = 0
    for node in map.nodeIter():
        if node.isIsolated():
            if map.removeIsolatedNode(node):
                result += 1
    return result

def mergeDegree2Nodes(map):
    """mergeDegree2Nodes(map)

    Removes all degree-2-nodes with map.mergeEdges(...) and
    returns the number of successful operations (= nodes removed)."""
    
    result = 0
    for node in map.nodeIter():
        if node.degree() == 2 and not node.anchor().edge().isLoop():
            if map.mergeEdges(node.anchor()):
                result += 1
    return result

def removeEdges(map, edgeLabels):
    """Removes all edges whose labels are in `edgeLabels`.
    Uses an optimized sequence of basic Euler operations."""
    
    result = 0

    bridges = []
    for edgeLabel in edgeLabels:
        edge = map.edge(edgeLabel)
        if edge.isBridge():
            bridges.append(edge)
        elif map.mergeFaces(edge.dart()):
            result += 1

    while bridges:
        # search for bridge with endnode of degree 1:
        for i, edge in enumerate(bridges):
            dart = edge.dart()
            if dart.endNode().degree() == 1:
                dart.nextAlpha()
                break
            # degree 1?
            if dart.clone().nextSigma() == dart:
                break
        
        while True:
            del bridges[i]
            next = dart.clone().nextPhi()
            if map.removeBridge(dart):
                result += 1
            if not next.edge():
                break
            # degree 1?
            if next.clone().nextSigma() != next:
                break
            dart = next
            try:
                i = bridges.index(dart.edge())
            except ValueError:
                break

    result += removeIsolatedNodes(map) # FIXME: depend on allowIsolatedNodes
    result += mergeDegree2Nodes(map)
    return result

def removeUnProtectedEdges(map):
    return removeEdges(map, [
        edge.label() for edge in map.edgeIter()
        if not edge.flag(flag_constants.ALL_PROTECTION)])

def removeSmallRegions(map, minArea):
    """Merge faces whose area is < minArea with any neighor.
    If there is more than one neighbor sharing an unprotected edge
    with the face, a warning is issued since it is not clear which
    neighbor to merge into."""
    result = 0
    for face in map.faceIter(skipInfinite = True):
        if face.area() >= minArea:
            continue

        # similar to neighborFaces, except for the protection check:
        neighbors = []
        seen = {face.label(): True}
        for dart in contourDarts(face):
            if dart.edge().flag(flag_constants.ALL_PROTECTION):
                continue
            fl = dart.rightFaceLabel()
            if not fl in seen:
                seen[fl] = True
                neighbors.append(dart)
        
        if len(neighbors) > 1:
            sys.stderr.write("WARNING: removeSmallRegions() has to decide about neighbor\n  which %s is to be merged into!\n" % face)

        for dart in neighbors:
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

def mergeFacesCompletely(dart, removeDegree2Nodes = True):
    """mergeFacesCompletely(dart, removeDegree2Nodes = True)

    In contrast to the Euler operation mergeFaces(), this function
    removes all common edges of the two faces, not only the single
    edge belonging to dart.

    Furthermore, if the optional parameter removeDegree2Nodes is
    True (default), all nodes whose degree is reduced to two will be
    merged into their surrounding edges.

    Returns the surviving face."""
    
    #print "mergeFacesCompletely(%s, %s)" % (dart, removeDegree2Nodes)
    if dart.edge().isBridge():
        raise TypeError("mergeFacesCompletely(): dart belongs to a bridge!")
    map = dart.map()
    rightLabel = dart.rightFaceLabel()
    commonEdgeList = []
    for contourIt in dart.phiOrbit():
        if contourIt.rightFaceLabel() == rightLabel:
            if contourIt.edge().flag(flag_constants.ALL_PROTECTION):
                return None
            commonEdgeList.append(contourIt)

    assert commonEdgeList, "mergeFacesCompletely(): no common edges found!"
    affectedNodes = []

    survivor = None
    for dart in commonEdgeList:
        affectedNodes.append(dart.startNodeLabel())
        affectedNodes.append(dart.endNodeLabel())
        if survivor == None:
            survivor = map.mergeFaces(dart) # first common edge
        else:
            map.removeBridge(dart)

    for nodeLabel in affectedNodes:
        node = map.node(nodeLabel)
        if not node: continue
        if node.degree == 0:
            map.removeIsolatedNode(node)
        if removeDegree2Nodes and node.degree() == 2:
            d = node.anchor()
            if d.endNodeLabel() != node.label():
                map.mergeEdges(d)

    return survivor

def findCommonDart(face1, face2):
    """Find a dart with leftFace() == face1 and rightFace() == face2."""
    for contour in face1.contours():
        for contourIt in contour.phiOrbit():
            if contourIt.rightFaceLabel() == face2.label():
                return contourIt

def mergeFacesByLabel(map, label1, label2, removeDegree2Nodes = True):
    """mergeFacesByLabel(map, label1, label2, removeDegree2Nodes = True)

    Similar to mergeFacesCompletely() (which is called to perform the
    action), but is parametrized with two face labels and finds a
    common dart of these labels.

    Returns the surviving face (or None if no common edge was found)."""
    
    face1 = map.face(label1)
    face2 = map.face(label2)
    assert face1, "mergeFacesByLabel: face with label1 = %d does not exist!" % (label1, )
    assert face2, "mergeFacesByLabel: face with label2 = %d does not exist!" % (label2, )
    dart = findCommonDart(face1, face2)
    return dart and mergeFacesCompletely(dart, removeDegree2Nodes)

# --------------------------------------------------------------------

class History(list):
    """List of (operation name, dart label) pairs.  Used to manage a
    list of operations that happend on a GeoMap.  For example, you may
    do:

    >>> map1 = ...
    >>> map2 = copy.copy(map1)
    >>> history = LiveHistory(map1)
    >>> someExpensiveAnalysisMethod(map1) # modify map1 via Euler ops
    >>> history.replay(map2)"""
    
    @staticmethod
    def load(filename):
        return History(eval(file(filename).read()))
    
    def save(self, filename):
        import pprint
        f = file(filename, "w")
        pprint.pprint(self, stream = f)
        f.close()

    def __getslice__(self, b, e):
        return History(list.__getslice__(self, b, e))

    def commands(self, mapName = "map"):
        """Return string with python commands replaying this history.
        (Very useful for creating code for unittests or the like.)"""
        result = ""
        for opName, label in self:
            result += "%s.%s(%s.dart(%d))\n" % (mapName, opName,
                                                mapName, label)
        return result

    def replay(self, map, careful = False, verbose = True):
        result = 0
        
#         counter = 20
        for opName, param in self:
            if careful and not map.checkConsistency():# or not checkLabelConsistency(map)
                sys.stderr.write(
                    "History.replay: map inconsistent, will not replay anything!\n")
                return result

            if verbose:
                print "replaying %s(dart %d)" % (opName, param)
            op = getattr(map, opName)
            op(map.dart(param))
            result += 1

#             counter -= 1
#             if not counter:
#                 qt.qApp.processEvents()
#                 counter = 20

        if careful:
            map.checkConsistency()
        return result

class LiveHistory(History):
    """`History` list which attaches to a GeoMap and updates itself
    via Euler operation callbacks."""
    
    _mergeFaces = 'mergeFaces'
    _removeBridge = 'removeBridge'
    _mergeEdges = 'mergeEdges'
    
    def __init__(self, map):
        self._attachedHooks = (
            map.addMergeFacesCallbacks(self.preMergeFaces, self.confirm),
            map.addRemoveBridgeCallbacks(self.preRemoveBridge, self.confirm),
            map.addMergeEdgesCallbacks(self.preMergeEdges, self.confirm),
            )
        
    def detachHooks(self):
        for cb in self._attachedHooks:
            cb.disconnect()
    
    def preMergeFaces(self, dart):
        self.op = (self._mergeFaces, dart.label())
        return True
    
    def preRemoveBridge(self, dart):
        self.op = (self._removeBridge, dart.label())
        return True
    
    def preMergeEdges(self, dart):
        self.op = (self._mergeEdges, dart.label())
        return True
    
    def confirm(self, *args):
        self.append(self.op)

# --------------------------------------------------------------------
#                      Automatic Region Merger
# --------------------------------------------------------------------

class AutomaticRegionMerger(object):
    """Merges faces in order of increasing costs.  The given
    mergeCostMeasure is called on a dart for each edge to determine
    the cost of removing that edge / merging the adjacent regions.

    Internally, a DynamicCostQueue is used in order to always remove
    the edge with the lowest assigned cost.  After each operation, the
    costs of all edges around the surviving face are recalculated."""

    __slots__ = ["_map", "_mergeCostMeasure", "_step", "_queue",
                 "_costLog"]

    def __init__(self, map, mergeCostMeasure, q = None):
        self._map = map
        self._mergeCostMeasure = mergeCostMeasure
        self._step = 0

        if q == None:
            q = hourglass.DynamicCostQueue(map.maxEdgeLabel()+1)
            for edge in map.edgeIter():
                cost = mergeCostMeasure(edge.dart())
                if cost is not None:
                    q.insert(edge.label(), cost)
        
        self._queue = q
        self._costLog = None

    def nextCost(self):
        """Returns the cost of the merge operation that would be
        performed next by `mergeStep()`."""
        return self._queue.top()[1]

    def nextEdgeLabel(self):
        """Returns the label of the dart that would be the parameter
        of the merge operation that would be performed next by
        `mergeStep()`."""
        return self._queue.top()[0]

    def step(self):
        """Returns the number of steps performed so far."""
        return self._step

    def merge(self, maxCost = None):
        oldStep = self._step
        if maxCost:
            self.mergeToCost(maxCost)
        else:
            while self._queue:
                self.mergeStep()
        return self._step - oldStep

    def mergeStep(self):
        """Fetch next edge from cost queue and remove it from the map.
        
        If the edge is protected or nonexistent, do nothing and return
        None (i.e. not every call results in a merge step!).  Else,
        return the surviving Face and increment the step counter
        (cf. step())."""
        
        edgeLabel, cost = self._queue.pop()
        edge = self._map.edge(edgeLabel)
        if not edge or edge.flag(flag_constants.ALL_PROTECTION):
            return

        d = edge.dart()
        if edge.isBridge():
            # FIXME: precondition "no bridges"?
            # or removeBridges in constructor?
            survivor = self._map.removeBridge(d)
        else:
            survivor = mergeFacesCompletely(d)
            if survivor:
                q = self._queue
                mcm = self._mergeCostMeasure
                for dart in contourDarts(survivor):
                    cost = mcm(dart)
                    if cost is not None:
                        q.setCost(dart.edgeLabel(), cost)

        if survivor:
            if self._costLog is not None:
                self._costLog.append(cost)
            self._step += 1

        return survivor

    def mergeSteps(self, count):
        return self.mergeToStep(self._step + count)

    def mergeToStep(self, targetStep):
        oldStep = self._step
        while self._queue and self._step < targetStep:
            self.mergeStep()
        return self._step - oldStep

    def mergeToCost(self, maxCost):
        oldStep = self._step
        while self._queue and self.nextCost() < maxCost:
            self.mergeStep()
        return self._step - oldStep

def thresholdMergeCost(map, mergeCostMeasure, maxCost, costs = None, q = None):
    """thresholdMergeCost(map, mergeCostMeasure, maxCost, costs = None, q = None)

    Merges faces of the given map in order of increasing costs until
    no more merges are assigned a cost <= maxCost by the
    mergeCostMeasure.

    The mergeCostMeasure is called on a dart for each edge to
    determine the cost of removing that edge.  This is used to
    initialize a DynamicCostQueue, which is used internally in order
    to always remove the edge with the lowest cost assigned.  The
    function returns a pair of the number of operations performed and
    the DynamicCostQueue, and you may pass the latter as optional
    argument 'd' into a subsequent call of thresholdMergeCost (with
    the same map and an increased maxCost) in order to re-use it
    (mergeCostMeasure is not used then).

    If the optional argument costs is given, it should be a mutable
    sequence which is append()ed the costs of each performed
    operation."""
    
    arm = AutomaticRegionMerger(map, mergeCostMeasure, q)
    arm._costLog = costs

    return arm.merge(maxCost = maxCost), arm._queue

# --------------------------------------------------------------------

def classifyFacesFromLabelImage(map, labelImage):
    """Given a labelImage, returns the label of each Face within that
    image.  This assumes that each region contains only pixels of the
    same label; otherwise an assertion will be triggered."""
    
    import statistics
    faceStats = statistics.FaceColorStatistics(map, labelImage)
    faceStats.detachHooks()

    result = [None] * map.maxFaceLabel()
    for face in map.faceIter(skipInfinite = True):
        l = faceStats[face.label()]
        assert float(int(l)) == l, "each Face must be entirely within one region of the labelImage"
        result[face.label()] = int(l)

    return result

def applyFaceClassification(map, faceClasses):
    """Removes all edges between faces with the same class/label.
    Uses `removeEdges()` internally.  `faceClasses` should be a
    mapping from face labels to labels/classes that can be compared
    for equality.  E.g. to faces face1 and face2 will be merged iff
    faceClasses[face1.label()] == faceClasses[face2.label()].
    
    `classifyFacesFromLabelImage` creates a suitable sequence (but
    there are better, more direct ways)."""

    removeEdges(map, [
        edge.label() for edge in map.edgeIter()
        if faceClasses[edge.leftFaceLabel()] == faceClasses[edge.rightFaceLabel()]])

def classifyEdgesFromLabelImage(map, labelImage):
    """FIXME: I think this API is old and should be deprecated.
    Should edge flags be used?  After all, it's a binary decision.
    Else, should one sequence of bools be returned?
    Or should it stay like this?
    The result should be easily passable to removeEdges() IMO."""
    faceLabels = classifyFacesFromLabelImage(map, labelImage)
    
    good = []
    bad = []
    for edge in map.edgeIter():
        if edge.flag(BORDER_PROTECTION | CONTOUR_PROTECTION):
            continue
        if faceStats[edge.leftFaceLabel()] != faceStats[edge.rightFaceLabel()]:
            good.append(edge)
        else:
            bad.append(edge)
    return good, bad

# --------------------------------------------------------------------
#                   topological utility functions
# --------------------------------------------------------------------

def contourDarts(face):
    """contourDarts(face)

    Generator function which iterates over all darts within all
    contours, i.e. substitues the following standard loop construct:

      for contour in face.contours():
          for dart in contour.phiOrbit():
              doSth(dart)

    Thus, you can now write:

      for dart in contourDarts(face):
          doSth(dart)"""

    for contour in face.contours():
        for dart in contour.phiOrbit():
            yield dart

def neighborFaces(face):
    """neighborFaces(face)

    Generator function which yields exactly one dart for each adjacent
    face (even in case of multiple common edges)."""
    
    seen = {face.label(): True}
    for dart in contourDarts(face):
        fl = dart.rightFaceLabel()
        if not fl in seen:
            seen[fl] = True
            yield dart

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
    for contour in face.holeContours():
        for hole in holeComponent(contour):
            showHomotopyTree(hole, indentation + "  ")

def edgeAtBorder(edge):
    """Return True iff edge has the BORDER_PROTECTION flag set."""
    return edge.flag(flag_constants.BORDER_PROTECTION)

def nonBorderEdges(map):
    """Iterate over all edges that do not have the BORDER_PROTECTION flag."""
    for edge in map.edgeIter():
        if not edge.flag(flag_constants.BORDER_PROTECTION):
            yield edge

def nodeAtBorder(node):
    """nodeAtBorder(node) -> bool

    Returns True iff the Node is adjacent to at least one edge
    marked with BORDER_PROTECTION."""
    if node.isIsolated():
        return False
    for dart in node.anchor().sigmaOrbit():
        if edgeAtBorder(dart.edge()):
            return True
    return False

def mapValidEdges(function, geomap, default = None):
    """Similar to map(function, geomap.edgeIter()), but preserves
    labels.  I.e., equivalent to [default] * geomap.maxEdgeLabel() and
    a successive filling in of function results for valid edges."""
    result = [default] * geomap.maxEdgeLabel()
    for edge in geomap.edgeIter():
        result[edge.label()] = function(edge)
    return result

# --------------------------------------------------------------------
#                       Seeded Region Growing
# --------------------------------------------------------------------

from heapq import heappush, heappop

def seededRegionGrowingStatic(map, faceLabels, edgeCosts):
    """seededRegionGrowingStatic(map, faceLabels, edgeCosts)

    Given a set of seed labels != None, extend labels to neighbor
    faces via edges with costs != None.  Stop when no more such
    growing is possible and return the resulting faceLabels.

    The 'static' postfix indicates that the edgeCosts do not change
    during the process.  (Furthermore, the GeoMap is not changed.)"""

    faceLabels = list(faceLabels)

    heap = []
    for face in map.faceIter():
        if not faceLabels[face.label()]:
            continue
        for dart in contourDarts(face):
            if not faceLabels[dart.rightFaceLabel()]:
                cost = edgeCosts[dart.edgeLabel()]
                if cost != None:
                    heappush(heap, (cost, dart.label()))
    while heap:
        _, dartLabel = heappop(heap)
        dart = map.dart(dartLabel)
        if faceLabels[dart.rightFaceLabel()]:
            continue # labelled in the meantime
        faceLabels[dart.rightFaceLabel()] = faceLabels[dart.leftFaceLabel()]
        for dart in contourDarts(dart.rightFace()):
            if not faceLabels[dart.rightFaceLabel()]:
                cost = edgeCosts[dart.edgeLabel()]
                if cost != None:
                    heappush(heap, (cost, dart.label()))
    
    return faceLabels

class StandardCostQueue(object):
    """Wrapper around the standard heappush/pop, implementing the
    DynamicCostQueue API."""
    
    def __init__(self):
        self.heap = []

    def insert(self, index, cost):
        heappush(self.heap, (cost, index))

    setCost = insert

    def empty(self):
        return not self.heap

    def __nonzero__(self):
        return bool(self.heap)

    def top(self):
        cost, index = self.heap[0]
        return index, cost

    def pop(self):
        cost, index = heappop(self.heap)
        return index, cost

class SeededRegionGrowing(object):
    __slots__ = ["_map", "_mergeCostMeasure", "_step", "_queue", "_neighborSkipFlags"]
    
    def __init__(self, map, mergeCostMeasure, dynamic = False, stupidInit = False):
        self._map = map
        self._mergeCostMeasure = mergeCostMeasure
        self._step = 0
        
        for face in map.faceIter():
            face.setFlag(flag_constants.SRG_BORDER, False)

        self._neighborSkipFlags = flag_constants.SRG_SEED
        if dynamic:
            self._queue = hourglass.DynamicCostQueue(map.maxFaceLabel())
        else:
            self._queue = StandardCostQueue()
            if stupidInit:
                self._neighborSkipFlags |= flag_constants.SRG_BORDER

        for face in map.faceIter():
            if face.flag(flag_constants.SRG_SEED):
                self._addNeighborsToQueue(face)

        assert self._queue, "SeededRegionGrowing: No seeds found (mark Faces with SRG_SEED)!"

        if not dynamic and not stupidInit:
            self._neighborSkipFlags |= flag_constants.SRG_BORDER

    def nextCost(self):
        return self._queue.top()[1]

    def step(self):
        return self._step

    def _addNeighborsToQueue(self, face):
        for dart in neighborFaces(face):
            neighbor = dart.rightFace()
            if not neighbor.flag(self._neighborSkipFlags):
                cost = self._mergeCostMeasure(dart)
                self._queue.setCost(neighbor.label(), cost)
                neighbor.setFlag(flag_constants.SRG_BORDER)

    def grow(self, maxCost = None):
        oldStep = self._step
        if maxCost:
            while self._queue and self.nextCost() <= maxCost:
                self.growStep()
        else:
            while self._queue:
                self.growStep()
        return self._step - oldStep

    def growStep(self):
        # fetch candidate Face from queue:
        while True:
            faceLabel, _ = self._queue.pop()
            face = self._map.face(faceLabel)
            if face and not face.flag(flag_constants.SRG_SEED):
                break

        assert face, "no more candidate faces found"

        # look for neighbor with lowest merge cost:
        best = None
        for dart in neighborFaces(face):
            neighbor = dart.rightFace()
            if neighbor.flag(flag_constants.SRG_SEED):
                cost = self._mergeCostMeasure(dart)
                if not best or cost < best[0]:
                    best = (cost, dart)

        # grow region:
        survivor = mergeFacesCompletely(best[1])
        if survivor:
            survivor.setFlag(flag_constants.SRG_BORDER, False)
            survivor.setFlag(flag_constants.SRG_SEED)
            self._addNeighborsToQueue(survivor)
            self._step += 1

    def growSteps(self, count):
        return self.growToStep(self._step + count)

    def growToStep(self, targetStep):
        oldStep = self._step
        while self._queue and self._step < targetStep:
            self.growStep()
        return self._step - oldStep

    def growToCost(self, maxCost):
        oldStep = self._step
        while self._queue and self.nextCost() < maxCost:
            self.growStep()
        return self._step - oldStep

def seededRegionGrowing(map, mergeCostMeasure, dynamic = False, stupidInit = False):
    """seededRegionGrowing(map, mergeCostMeasure, dynamic = False, stupidInit = False)

    Implements Seeded Region Growing [Adams, Bischof].  Expects seed
    faces to be marked with the SRG_SEED flag.  Modifies the map by
    merging all non-seed faces into the growing seed regions until
    only these are left (or no more merges are possible, e.g. due to
    edge protection).

    The optional parameter 'dynamic' decides whether the costs of each
    possible merge shall be updated in each step.

    If stupidInit is set, the initialization is done exactly as in the
    original paper - the difference is that a face adjacent to two
    seeds will be associated the cost of merging it into the neighbor
    with the lower label (processing order) instead of the minimum of
    all costs.  Note that this streamlines the algorithm, but is
    dangerous when using superpixels, which makes the mentioned case
    happen regularly (instead of being an exception as in the original
    pixel-based SRG application)."""

    srg = SeededRegionGrowing(map, mergeCostMeasure, dynamic, stupidInit)
    srg.grow()

# --------------------------------------------------------------------
#                          MST / waterfall
# --------------------------------------------------------------------

def minimumSpanningTree(map, edgeCosts):
    """minimumSpanningTree(map, edgeCosts)

    Given a cost associated with each edge of the map, this function
    finds the minimum spanning tree of the map's boundary graph (None
    in edgeCosts is allowed and is handled as if the corresponding
    edge was missing).  The result is a modified copy of the edgeCosts
    list, with all non-MST-edges set to None.  This can be used for
    the waterfall algorithm by Meyer and Beucher, see waterfall().

    The complexity is O(edgeCount*faceCount), but the performance
    could be improved a little."""

    faceLabels = [None] * map.maxFaceLabel()
    for face in map.faceIter():
        faceLabels[face.label()] = face.label()

    print "- initializing priority queue for MST..."
    heap = []
    for edge in map.edgeIter():
        edgeLabel = edge.label()
        cost = edgeCosts[edgeLabel]
        if cost != None:
            heappush(heap, (cost, edgeLabel))

    print "- building MST..."
    result = list(edgeCosts)
    while heap:
        _, edgeLabel = heappop(heap)
        edge = map.edge(edgeLabel)
        lfl = faceLabels[edge.leftFaceLabel()]
        rfl = faceLabels[edge.rightFaceLabel()]
        if lfl != rfl:
            for i in range(len(faceLabels)): # this could be optimized
                if faceLabels[i] == rfl:
                    faceLabels[i] = lfl
        else:
            result[edgeLabel] = None

    return result

def regionalMinima(map, mst):
    """regionalMinima(map, mst)

    Returns an array with face labels, where only 'regional minima' of
    the given MSt are labelled.  See the article 'Fast Implementation
    of Waterfall Based on Graphs' by Marcotegui and Beucher."""

    faceLabels = [None] * map.maxFaceLabel()
    for edge in map.edgeIter():
        edgeCost = mst[edge.label()]
        if edgeCost == None:
            continue
        isMinimum = True
        for start in edge.dart().alphaOrbit():
            for dart in contourDarts(face):
#               if dart == start: # not needed for strict comparison below
#                   continue
                otherCost = mst[dart.edgeLabel()]
                if otherCost != None and otherCost < edgeCost:
                    isMinimum = False
                    break
            if not isMinimum:
                break
        if isMinimum:
            faceLabels[edge.leftFaceLabel()] = edge.label()
            faceLabels[edge.rightFaceLabel()] = edge.label()
    return faceLabels

def waterfallLabels(map, edgeCosts, mst = None):
    """waterfallLabels(map, edgeCosts, mst = None)

    Perform a single iteration of the waterfall algorithm by
    Marcotegui and Beucher and return an array with face labels."""

    if not mst:
        mst = minimumSpanningTree(map, edgeCosts)

    print "- finding regional minima..."
    faceLabels = regionalMinima(map, mst)

    print "- extending faceLabels via MST..."
    return seededRegionGrowingStatic(map, faceLabels, mst)

def waterfall(map, edgeCosts, mst = None):
    """waterfall(map, edgeCosts, mst = None)

    Perform a single iteration of the waterfall algorithm by
    Marcotegui and Beucher and perform the face merging in the given
    map."""

    c = time.clock()
    faceLabels = waterfallLabels(map, edgeCosts, mst)
    print "- merging regions according to labelling..."
    for edge in map.edgeIter():
        if edge.flag(flag_constants.ALL_PROTECTION):
            continue
        if edge.isBridge():
            print "ERROR: waterfall should not be called on maps with bridges!"
            map.removeBridge(edge.dart())
            continue
        lfl = faceLabels[edge.leftFaceLabel()]
        rfl = faceLabels[edge.rightFaceLabel()]
        if lfl == rfl:
            mergeFacesCompletely(edge.dart())
    print "  total waterfall() time: %ss." % (time.clock() - c, )

def mst2map(mst, map):
    """Visualize a minimumSpanningTree() result as a GeoMap.
    Note that the nodes are located at the centroids of the faces,
    which is only a heuristic."""

    nodePositions = [None] * map.maxFaceLabel()
    for face in map.faceIter(skipInfinite = True):
        nodePositions[face.label()] = centroid(contourPoly(face.contour()))
    
    edgeTuples = [None]
    for edgeLabel in mst:
        edge = map.edge(edgeLabel)
        snl = edge.leftFaceLabel()
        enl = edge.rightFaceLabel()
        edgeTuples.append((snl, enl, [nodePositions[snl], nodePositions[enl]]))
    
    result = hourglass.GeoMap(nodePositions, edgeTuples, map.imageSize())
    removeIsolatedNodes(result)
    result.initializeMap(False)
    return result

# --------------------------------------------------------------------
#                               tests
# --------------------------------------------------------------------

# if __name__ == '__main__':
    
