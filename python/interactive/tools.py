"""tools - module with interactive GeoMap tools"""

import sys
from PyQt4 import QtCore, QtGui
import geomap, maputils
from maputils import mergeFacesByLabel
from flag_constants import FOREGROUND_FACE, BACKGROUND_FACE, SRG_SEED
from vigrapyqt4 import EdgeOverlay, PointOverlay
from qimageviewertool import QImageViewerTool

__all__ = ["MapSearcher", "ManualClassifier", "ActivePaintbrush", "SeedSelector",
           "IntelligentScissors", "LiveWire"]

# --------------------------------------------------------------------

class MapSearcher(QImageViewerTool):
    def __init__(self, map, display):
        QImageViewerTool.__init__(self, display)
        self._map = map
        self.display = display
        self.connectViewer(display.viewer)

    def mousePressed(self, x, y, button):
        if button != QtCore.Qt.LeftButton or self.activeModifiers():
            return False
        nearestNode = self._map.nearestNode((x, y))
        #sys.stdout.write("Node %d is %.2f from %d/%d\n" % (nearestNode.label(), minDist, x, y))
        if nearestNode.hasMinDegree(1):
            self.display.navigate(nearestNode.anchor(), center = False)
        return True

# --------------------------------------------------------------------

class _OldManualClassifier(QImageViewerTool):
    classes = [None, False, True]

    def __init__(self, map, foreground, parent = None):
        QImageViewerTool.__init__(self, parent)
        self._map = map
        self.foreground = foreground
        self.manual = {}
        self.connectViewer(parent.viewer)

    def mousePressed(self, x, y, button):
        if button != QtCore.Qt.LeftButton or self.activeModifiers():
            return False
        face = self._map.faceAt((x, y))
        oldClass = self.foreground[face.label()]
        newClass = self.classes[(self.classes.index(oldClass) + 1) % 3]
        self.foreground[face.label()] = newClass
        self.manual[face.label()] = newClass
        print "manually changed face %d to %s" % (face.label(), newClass)
        self.emit(QtCore.SIGNAL("classChanged"), face)
        return True

# --------------------------------------------------------------------

class ManualClassifier(QImageViewerTool):
    """Allows interactive classification of GeoMap faces.  Also
    manages a list of FaceOverlays to display the current
    classification.

    Faces can be clicked to toggle their class (LMB/MMB for
    forward/backward cycling through classes), or strokes can be used
    to transfer the class of a face to its neighbors."""
    
    __slots__ = ("manual",
                 "_map", "_classes", "_classMask", "_overlays",
                 "_enabled", "_pressed",
                 "_toggling", "_paintClassIndex", "_currentLabel")

    def __init__(self, map, edgeOverlay,
                 classes = (FOREGROUND_FACE,
                            BACKGROUND_FACE,
                            0),
                 classNames = None,
                 colors = (QtCore.Qt.yellow, QtCore.Qt.cyan),
                 filter = None,
                 parent = None):
        """Initialize and start the ManualClassifier tool.

        The edgeOverlay is needed for the internal MapFaces overlays
        (for efficient implementation), often one simply passes the
        .edgeOverlay attribute of the MapDisplay.

        The `classes` can be user-defined flags and default to
        FOREGROUND_FACE/BACKGROUND_FACE/no flag (see flag_constants
        module).  The `colors` array should have the same size
        (otherwise it is cropped or padded with None) and is used to
        initialize corresponding MapFaces overlays for displaying the
        classes (a class with a color of None will not be displayed).

        If a `filter` is given, it is called for every face to
        determine whether its classification may be changed at all.
        For instance, this can be used to make sure that only objects
        are classified, and the background region is never assigned to
        some object class.

        For display and interaction reasons, the given parent (usually
        a MapDisplay) *must* have a 'viewer' attribute."""

        QImageViewerTool.__init__(self, parent)

        self.manual = {}

        self._map = map
        self._classes = list(classes)
        if len(colors) != len(classes):
            colors = list(colors)[:len(classes)]
            if len(colors) < len(classes):
                colors += [None] * (len(classes) - len(colors))

        self._classMask = 0
        for c in classes:
            self._classMask |= c

        import mapdisplay
        # (nb: MapFaces cannot display faces with flags = 0)
        self._overlays = [
            mapdisplay.MapFaces(map, edgeOverlay, color = c, flags = f)
            for c, f in zip(colors, classes) if c and f]
        if classNames:
            for o in self._overlays:
                i = self._classes.index(o.flags)
                o.name = classNames[i]

        self.filter = filter

        self._enabled = True
        self._pressed = False
        self._toggling = False

        self.connectViewer(parent.viewer)
        for o in self._overlays:
            viewer.addOverlay(o)

    def setEnabled(self, onoff):
        self._enabled = onoff

    def findSeeds(self):
        classFaces = maputils.extractFaceClasses(self._map, self._classes)
        result = {}
        for flag, faces in classFaces.items():
            result[flag] = [maputils.pointInFace(face) for face in faces]
        return result

    def setClassIndex(self, face, newClassIndex):
        face.setFlag(self._classMask, False)
        face.setFlag(self._classes[newClassIndex])
        self.manual[face.label()] = newClassIndex
        #print "manually changed face %d to %s" % (face.label(), newClassIndex)
        self._overlays[0].updateFaceROI(face)
        self.emit(QtCore.SIGNAL("classChanged"), face)

    def toggleOverlays(self, onoff = None):
        if onoff is None:
            onoff = not self._overlays[0].visible
        for o in self._overlays:
            o.visible = onoff
        viewer = self.parent().viewer
        viewer.update()

    def mousePressed(self, x, y, button):
        if not self._enabled:
            return False
        if button not in (QtCore.Qt.LeftButton, QtCore.Qt.MidButton) or self.activeModifiers():
            return False
        face = self._map.faceAt((x, y))
        if self.filter and not self.filter(face):
            return False

        try:
            self._paintClassIndex = self._classes.index(
                face.flag(self._classMask))
        except IndexError:
            return True

        self._pressed = button
        self._toggling = True
        self._currentLabel = face.label()
        return True

    def mouseMoved(self, x, y):
        if not self._pressed:
            return

        face = self._map.faceAt((x, y))
        if self._currentLabel == face.label():
            return

        # moved into a different face:
        self._currentLabel = face.label()
        self._toggling = False

        if self.filter and not self.filter(face):
            return

        # apply painting classification to that face, too:
        if self._classes.index(
            face.flag(self._classMask)) != self._paintClassIndex:
            self.setClassIndex(face, self._paintClassIndex)

    def mouseReleased(self, x, y, button):
        if button not in (QtCore.Qt.LeftButton, QtCore.Qt.MidButton):
            return False
        if not self._pressed:
            return False

        if self._toggling:
            face = self._map.face(self._currentLabel)
            classes = self._classes
            offset = self._pressed == QtCore.Qt.LeftButton \
                     and 1 or (len(classes) - 1)
            self.setClassIndex(
                face, (self._paintClassIndex + offset) % len(classes))
            self._toggling = False

        self._pressed = False
        return True

    def disconnectViewer(self):
        for o in self._overlays:
            self._viewer.removeOverlay(o)
        QImageViewerTool.disconnectViewer(self)

# --------------------------------------------------------------------

class SeedSelector(QImageViewerTool):
    def __init__(self, map = None, markFlags = SRG_SEED,
                 parent = None):
        QImageViewerTool.__init__(self, parent)
        self.seeds = []
        self._seedMap = None

        self.overlay = PointOverlay(self.seeds, QtCore.Qt.cyan, 2)
        viewer.addOverlay(self.overlay)

        self.map = map
        self.markFlags = markFlags

        self.connectViewer(parent.viewer)

    def setSeeds(self, seeds):
        self.seeds = seeds
        self._seedMap = None
        self.overlay.setPoints(seeds)

    def seedMap(self):
        if not self._seedMap:
            self._seedMap = geomap.PositionedMap()
            for pos in self.seeds:
                self._seedMap.insert(pos, pos)
        return self._seedMap

    def mousePressed(self, x, y, button):
        if button != QtCore.Qt.LeftButton or self.activeModifiers():
            return False

        viewer = self.parent().viewer

        if not self.activeModifiers() & QtCore.Qt.ControlModifier:
            seed = (x, y)
            self.seeds.append(seed)
            self.emit(QtCore.SIGNAL("seedAdded"), seed)
            if self.map and self.markFlags:
                self.map.faceAt(seed).setFlag(self.markFlags)
        else:
            seed = self.seedMap()((x, y), 10./viewer.zoomFactor())
            if not seed:
                return False
            self.seeds.remove(seed)
            self._seedMap.remove(seed)
            self.emit(QtCore.SIGNAL("seedRemoved"), seed)
            if self.map and self.markFlags:
                self.map.faceAt((x, y)).setFlag(self.markFlags, False)
        
        self.overlay.setPoints(self.seeds)
        viewer.update()
        return True

    def disconnectViewer(self):
        self._viewer.removeOverlay(self.overlay)
        QImageViewerTool.disconnectViewer(self)

# --------------------------------------------------------------------

from flag_constants import PROTECTED_FACE

class ActivePaintbrush(QImageViewerTool):
    def __init__(self, map, parent = None):
        QImageViewerTool.__init__(self, parent)
        self._map = map
        self._painting = False
        self._currentLabel = None
        self._changed = None
        s = map.imageSize()
        self._mapArea = s.width * s.height

        self.connectViewer(parent.viewer)

    def mousePressed(self, x, y, button):
        if button != QtCore.Qt.LeftButton or self.activeModifiers():
            return False
        if not self._map.mapInitialized():
            sys.stderr.write("Paintbrush: Map not initialized. Unable to determine faces.\n")
            return False
        self._currentLabel = None
        self._painting = True
        self._path = []
        self._changed = False
        self.mouseMoved(x, y)
        self.emit(QtCore.SIGNAL("paintbrushStarted"),
                  self._map.face(self._currentLabel))
        return True

    def mouseMoved(self, x, y):
        if not self._painting:
            return

        self._path.append((x, y))

        map = self._map
        otherLabel = map.faceAt((x, y)).label()
        if otherLabel == 0 and map.face(0).area() < -self._mapArea + 1:
            currentLabel = None
            return
        if self._currentLabel == None:
            self._currentLabel = otherLabel
        if self._currentLabel == otherLabel:
            return

        try:
            survivor = mergeFacesByLabel(map, self._currentLabel, otherLabel)
            if survivor:
                self._changed = True
                self._currentLabel = survivor.label()
            else:
                self._currentLabel = otherLabel
        except Exception, e:
            sys.stderr.write("Paintbrush: Merge operation failed. Cancelling paint mode.\n")
            self._painting = False
            raise

    def mouseReleased(self, x, y, button):
        if button != QtCore.Qt.LeftButton:
            return False
        if not self._painting:
            return False
        self._painting = False
        if self._changed:
            self.emit(QtCore.SIGNAL("paintbrushFinished"),
                      self._map.face(self._currentLabel))
        return True

    def mouseDoubleClicked(self, x, y, button):
        if button != QtCore.Qt.LeftButton:
            return False
        face = self._map.faceAt((x, y))
        maputils.protectFace(face, not face.flag(PROTECTED_FACE))
        self.emit(QtCore.SIGNAL("faceProtectionChanged"), face)
        return True

# --------------------------------------------------------------------

from heapq import heappush, heappop # requires Python 2.3+

from flag_constants import CURRENT_CONTOUR, SCISSOR_PROTECTION, BORDER_PROTECTION

class LiveWire(object):
    """Represents a live wire path and manages path search.
    
    The LiveWire class does not only represent a single live wire
    path, but also performs the complete path search in a dynamic
    programming fashion, i.e. finding the optimal paths to all
    reachable nodes.  You can then immediately switch between desired
    end nodes by calling setEndNodeLabel().

    Edges that are marked with the CURRENT_CONTOUR flag are avoided
    (use this to prevent going the same way back)."""

    def __init__(self, map, costMeasure, startNodeLabel):
        self._map = map
        self._costMeasure = costMeasure
        if hasattr(startNodeLabel, "label"):
            startNodeLabel = startNodeLabel.label()
        self._startNodeLabel = startNodeLabel
        self._endNodeLabel = startNodeLabel

        self._nodePaths = [None] * (self._map.maxNodeLabel() + 1)
        self._nodePaths[self._startNodeLabel] = (0.0, None)

        self._searchBorder = []

        # similar to _expandNode, but the latter starts from the end
        # of a dart, which we do not have yet here:
        for dart in map.node(startNodeLabel).anchor().sigmaOrbit():
            if dart.edge().flag(CURRENT_CONTOUR):
                continue
            heappush(self._searchBorder, (
                costMeasure(dart), dart.label()))

    def startNodeLabel(self):
        """liveWire.startNodeLabel() -> int

        Returns label of the Node at the beginning of the current live
        wire's path."""

        return self._startNodeLabel

    def endNodeLabel(self):
        """liveWire.endNodeLabel() -> int

        Returns label of the Node at the beginning of the current live
        wire's path."""

        return self._endNodeLabel

    def expandBorder(self):
        """liveWire.expandBorder()

        Performs a single step of the dynamic programming for finding
        all optimal paths from the start node.  Returns False iff the
        process finished.  Call this e.g. from an idle loop as long as
        it returns True.

        Internally, picks cheapest path from searchBorder, and if no
        path to its end node is known yet, stores it and calls
        _expandNode() to proceed with its neighbor nodes."""

        if not len(self._searchBorder):
            return False

        path = heappop(self._searchBorder)
        endNodeLabel = self._map.dart(path[1]).endNodeLabel()
        if not self._nodePaths[endNodeLabel]: # or self._nodePaths[endNodeLabel][0] > path[0]
            self._nodePaths[endNodeLabel] = path
            self._expandNode(endNodeLabel)

        return True

    def expandToNode(self, nodeLabel):
        """Expand (via expandBorder()) until a path to the indicated
        node is known."""

        while not self._nodePaths[nodeLabel]:
            if not self.expandBorder():
                return False
        return True

    def expandToCost(self, cost):
        """Expand (via expandBorder()) until all paths < cost are known."""

        while self._searchBorder and self._searchBorder[0][0] < cost:
            self.expandBorder()
        return bool(self._searchBorder)

    def expand(self):
        """Expand completely, until expandBorder() returns False."""

        while self.expandBorder():
            pass

    def _expandNode(self, nodeLabel):
        """Add all neighbors of the given node to the searchBorder."""

        prevPath = self._nodePaths[nodeLabel]
        sigmaOrbit = self._map.dart(-prevPath[1]).sigmaOrbit()
        sigmaOrbit.next() # skip prevPath[1] where we're coming from

        for dart in sigmaOrbit:
            if dart.edge().flag(CURRENT_CONTOUR | BORDER_PROTECTION):
                continue
            heappush(self._searchBorder, (
                prevPath[0] + self._costMeasure(dart), dart.label()))

    def setEndNodeLabel(self, nodeLabel):
        """Try to set the live wire's end node to the given one.
        Returns ``True`` iff successful, i.e. a path to that node is
        already known.  You can then call `pathDarts()` to query the
        darts belonging to that path or `totalCost()` to get the cost
        of that path.."""

        if self._nodePaths[nodeLabel]:
            self._endNodeLabel = nodeLabel
            return True

    def loopPath(self, nodeLabel):
        """Return additional path segment from nodeLabel to
        endNodeLabel.  If the optimal path from startNodeLabel to
        `nodeLabel` (i.e. pathDarts(nodeLabel)) passes endNodeLabel,
        return this (e.g. loop closing) path segment, else return
        None. """
        
        if self._nodePaths[nodeLabel]:
            result = []
            for dart in self.pathDarts(nodeLabel):
                result.append(dart)
                if dart.endNodeLabel() == self._endNodeLabel:
                    return result

    def pathDarts(self, endNodeLabel = None):
        """Generator function returning all darts along the current live
        wire path, ordered and pointing back from the current
        endNodeLabel() to startNodeLabel()."""

        nl = endNodeLabel
        if nl == None:
            nl = self._endNodeLabel
        while nl != self._startNodeLabel:
            dart = self._map.dart(-self._nodePaths[nl][1])
            yield dart
            nl = dart.endNodeLabel()

    def totalCost(self, endNodeLabel = None):
        if endNodeLabel == None:
            endNodeLabel = self._endNodeLabel
        return self._nodePaths[endNodeLabel][0]

class IntelligentScissors(QImageViewerTool):
    def __init__(self, map, mapEdges, parent = None):
        QImageViewerTool.__init__(self, parent)
        self._map = map

        self._liveWire = None
        self._loopNodeLabel = None
        self._startNodeLabel = None
        self._loop = None
        self._contour = [] # darts within current (multi-segment) contour
        self._prevContour = None # last finished _contour
        self._seeds = [] # all seeds of all contours (for debugging ATM)
        self._expandTimer = QtCore.QTimer(self)
        self._mapEdges = mapEdges
        self.connect(self._expandTimer, QtCore.SIGNAL("timeout()"),
                     self._expandBorder)

        self.connectViewer(parent.viewer)
        self._overlay = PointOverlay([], QtCore.Qt.green, 1)
        self._viewer.addOverlay(self._overlay)

    def disconnectViewer(self):
        self._viewer.removeOverlay(self._overlay)
        QImageViewerTool.disconnectViewer(self)

    def startContour(self):
        self._loopNodeLabel = self._startNodeLabel

    def startLiveWire(self):
        """Start a LiveWire at self._startNodeLabel.
        Starts a QTimer for repeated calling of _expandBorder()."""

        self._seeds.append(self._startNodeLabel)

        if not self._liveWire or \
               self._liveWire.startNodeLabel() != self._startNodeLabel:
            self._liveWire = LiveWire(
                self._map, activeCostMeasure, self._startNodeLabel)

        self._expandTimer.start(0)

    def stopLiveWire(self):
        """Stop the LiveWire (i.e., the QTimer)."""

        self._expandTimer.stop()

    def stopCurrentContour(self):
        """Called when the current contour is stopped, e.g. with
        middle MB (cancel) or with a LMB double click (confirm)."""
        
        self.stopLiveWire()

        #updateViewer(self.currentPathBounds)
        self._startNodeLabel = self._liveWire.endNodeLabel()
        self._loopNodeLabel = None # not really needed I think
        self._liveWire = None

        if self._contour:
            for dart in self._contour:
                dart.edge().setFlag(CURRENT_CONTOUR, False)
            self.emit(QtCore.SIGNAL("contourFinished"), self._contour)
            self._prevContour = self._contour
            self._contour = []

    def _expandBorder(self):
        if not self._liveWire.expandBorder():
            self._expandTimer.stop()
            return

    def mousePressed(self, x, y, button):
        """With left mouse button, the live wire is started, with the
        middle mouse button it can be cancelled."""

        if self.activeModifiers():
            return False

        if button == QtCore.Qt.RightButton:
            if self._liveWire:
                self.stopCurrentContour()
                return True
            return False

        if button == QtCore.Qt.LeftButton:
            if not self._liveWire:
                self.startContour()
            else:
                self.stopLiveWire()
                self._protectPath(self._liveWire.pathDarts())
                self._startNodeLabel = self._liveWire.endNodeLabel()
        
        o = EdgeOverlay([], QtCore.Qt.yellow, 2)
        self._viewer.replaceOverlay(o, self._overlay)
        self._overlay = o

        self.startLiveWire()
        return True

    def mouseMoved(self, x, y):
        """It the live wire is active, it's end node is set to the
        nearest node and the display (overlay) is updated. Else, the
        nearest node is chosen as start node and highlighted."""

        node = self._map.nearestNode((x, y))
        if not self._liveWire:
            if node.label() != self._startNodeLabel:
                self._startNodeLabel = node.label()
                o = PointOverlay([node.position()], QtCore.Qt.green, 1)
                self._viewer.replaceOverlay(o, self._overlay)
                self._overlay = o
        else:
            if node.label() != self._liveWire.endNodeLabel():
                if self._liveWire.setEndNodeLabel(node.label()): # TODO: else... (delayed)
                    pathEdges = [dart.edge()
                                 for dart in self._liveWire.pathDarts()]
                    self._loop = self._liveWire.loopPath(self._loopNodeLabel)
                    if self._loop:
                        pathEdges.extend([dart.edge() for dart in self._loop])
                    o = EdgeOverlay(pathEdges, QtCore.Qt.yellow, 2)
                    self._viewer.replaceOverlay(o, self._overlay)
                    self._overlay = o

    def _protectPath(self, darts):
        protect = not self.activeModifiers() & QtCore.Qt.ControlModifier

        edges = []
        for dart in darts:
            edge = dart.edge()
            edge.setFlag(SCISSOR_PROTECTION | CURRENT_CONTOUR, protect)
            edges.append(edge)
            self._contour.append(dart)
        self._mapEdges.updateEdgeROI(edges)

    def mouseDoubleClicked(self, x, y, button):
        """With a double left click, the current live wire is fixed
        (by mousePressed) and becomes inactive.  If _loop is available
        (i.e. a path back to the starting node), this becomes
        protected in addition to the last segment."""

        if button == QtCore.Qt.LeftButton and not self.activeModifiers():
            if self._loop:
                self._protectPath(self._loop)
                self._loop = None
                self._seeds.append(self._loopNodeLabel)

            self.stopCurrentContour()
            return True

        return False

# --------------------------------------------------------------------

# FIXME: This shouldn't be set here:
activeCostMeasure = None
