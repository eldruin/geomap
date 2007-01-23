import copy
from vigra import Point2D, Vector2, Rational

def freeman(crackEdge):
    """Returns freeman codes for each polyline segment in the given
    crackEdge polygon.  Uses mathematical orientation, i.e. the code
    meaning is:
    
    0: diff = ( 1,  0)
    1: diff = ( 0,  1)
    2: diff = (-1,  0)
    3: diff = ( 0, -1)
    """
    result = [None] * (len(crackEdge)-1)
    it = iter(crackEdge)
    prev = it.next()
    i = 0
    for point in it:
        diff = point - prev
        if diff[0]:
            if diff[0] > 0:
                result[i] = 0
            else:
                result[i] = 2
        elif diff[1] < 0:
            result[i] = 3
        else:
            result[i] = 1
        i += 1
        prev = point
    return result

freeman2Diff8Conn    = [Point2D( 1, 0), Point2D( 1,  1)]
freeman2Diff8ConnNeg = [Point2D(-1, 0), Point2D(-1, -1)]
freeman2Diff4Conn    = [Point2D( 1, 0), Point2D( 0,  1)]

def quadrant(c1, c2):
    if c2 < c1:
        c1, c2 = c2, c1
    if c1 > 0:
        return c1
    if c2 > 1:
        return 3
    return 0

def forwardIter(list, index, loop):
    i = index
    while i < len(list):
        yield list[i]
        i += 1
    if loop:
        i = 0
        while i < index:
            yield list[i]
            i += 1

def backwardIter(list, index, loop):
    i = index
    while i >= 0:
        yield list[i]
        i -= 1
    if loop:
        i = len(list) - 1
        while i >= 0:
            yield list[i]
            i -= 1

def searchForwardQuadrant(freemanCodes, index, closed = True):
    it = forwardIter(freemanCodes, index, closed)
    c1 = it.next()
    try:
        while True:
            c2 = it.next()
            if c2 != c1:
                return (c1, c2)
    except StopIteration:
        return c1

def searchBackwardQuadrant(freemanCodes, index, closed = True):
    it = backwardIter(freemanCodes, index, closed)
    c1 = it.next()
    try:
        while True:
            c2 = it.next()
            if c2 != c1:
                return (c1, c2)
    except StopIteration:
        return c1

def straightPoints(poly):
    closed = poly[0] == poly[-1]
    freemanCodes = freeman(poly)
    result = []
    for i in range(len(poly)-1):
        fc = searchForwardQuadrant(freemanCodes, i, closed)
        bc = searchBackwardQuadrant(freemanCodes, i, closed)
        if fc != bc:
            result.append(poly[i])
    return result

class DigitalStraightLine(object):
    def __init__(self, a, b, pos = 0):
        self.a = a
        self.b = b
        self.pos = pos
        self.is8Connected = True
    
    def slope(self):
        return Rational(self.a, self.b)
    
    def axisIntercept(self, leaningType = 0):
        """dsl.axisIntercept(leaningType = 0)

        leaningType means:
        0: center line
        1: lower leaning line
        2: upper leaning line"""
        
        pos = Rational(self.pos, 1)
        if leaningType == 0:
            pos += Rational(self.thickness()-1, 2)
        elif leaningType == 1:
            pos += self.thickness()-1
        return -pos / self.b
    
    def plotEquation(self, leaningType = 0):
        return ("%s*x + (%s)" % (self.slope(), self.axisIntercept(leaningType))).replace("/", "./")
    
    def __call__(self, point):
        return self.a*point[0] - self.b*point[1]
    
    def __getinitargs__(self):
        return (self.a, self.b, self.pos)
    
    def contains(self, point):
        v = self(point) - self.pos
        return 0 <= v < self.thickness()
    
    def thickness(self):
        if self.is8Connected:
            return max(abs(self.a), abs(self.b))
        else:
            return abs(self.a) + abs(self.b)
    
    def __repr__(self):
        return "DigitalStraightLine(%d, %d, %d)" % (self.a, self.b, self.pos)
    
    def addPoint(self, point):
        """works only for 8-connected lines in 1st octant"""
        assert self.is8Connected
        v = self(point) - self.pos
        width = self.thickness()
        if 0 <= v < width:
            #print "point already inside:", self, point
            return True # point is within DSL

        above = True
        if v == -1:
            # point above
            pass
        elif v == width:
            # point below
            above = False
        else:
            #print point, "cannot be added to", self, self.contains(point)
            return False

        y = abs(point[1])
        x = point[0]
        if point[1] < 0: # since sign(0) == 0, let's be safe here.. :-(
            x = -x
        
        if above:
            #print "slope increases:", self, point
            k = 0
            while k < width:
                if (self.a*k-self.pos) % width == 0:
                    break
                k += 1
            assert k < width
            l = (self.a*k-self.pos) / width
            self.a = y - l
            self.b = x - k
            # ensure new point is on lower leaning line:
            self.pos = self(point)
        else:
            #print "slope decreases:", self, point
            k = 0
            while k < width:
                if (self.a*k-self.pos-width+1) % width == 0:
                    break
                k += 1
            assert k < width
            l = (self.a*k-self.pos-width+1) / width
            self.a = y - l
            self.b = x - k
            # ensure new point is on upper leaning line:
            self.pos = self(point) - self.thickness() + 1

        if not self.contains(point):
            print self, v, point
            assert self.contains(point), \
                   "post-condition: addPoint should lead to contains"
        return True
    
    def convert8to4(self):
        self.b = self.b - self.a
        self.is8Connected = False

def originatingPolyIter(freemanIter, allowed, freeman2Diff):
    cur = Point2D(0, 0)
    for fc in freemanIter:
        if not fc in allowed:
            return
        cur += freeman2Diff[fc % 2]
        yield copy.copy(cur)

def _dirDSL(startCode, freemanIter, allowed):
    dsl = DigitalStraightLine(startCode % 2 and 1 or 0, 1, 0)
    for point in originatingPolyIter(freemanIter, allowed):
        if not dsl.addPoint(point):
            break
    return dsl

def forwardDSL(freemanCodes, index, closed, allowed = None):
    if allowed == None:
        allowed = searchForwardQuadrant(freemanCodes, index, closed)
    fmi = forwardIter(freemanCodes, index, closed)
    dsl = DigitalStraightLine(freemanCodes[index] % 2 and 1 or 0, 1, 0)
    for point in originatingPolyIter(freemanIter, allowed, freeman2Diff8Conn):
        if not dsl.addPoint(point):
            break
    return dsl, fmi.gi_frame.f_locals["i"]-index

def backwardDSL(freemanCodes, index, closed, allowed = None):
    if allowed == None:
        allowed = searchForwardQuadrant(freemanCodes, index, closed)
    fmi = backwardIter(freemanCodes, index, closed)
    dsl = DigitalStraightLine(freemanCodes[index] % 2 and 1 or 0, 1, 0)
    for point in originatingPolyIter(freemanIter, allowed, freeman2Diff8ConnNeg):
        if not dsl.addPoint(point):
            break
    return dsl, -(fmi.gi_frame.f_locals["i"]-index)

def tangentDSL(freemanCodes, index, closed, allowed = None):
    if allowed == None:
        allowed = searchForwardQuadrant(freemanCodes, index, closed)
    dsl = DigitalStraightLine(freemanCodes[index] % 2 and 1 or 0, 1, 0)
    result = dsl
    ffmi = forwardIter(freemanCodes, index, closed)
    bfmi = backwardIter(freemanCodes, index, closed)
    bopi = originatingPolyIter(bfmi, allowed, freeman2Diff8ConnNeg)
    for point1 in originatingPolyIter(ffmi, allowed, freeman2Diff8Conn):
        try:
            point2 = bopi.next()
        except StopIteration:
            break
        result = copy.copy(dsl)
        if not dsl.addPoint(point1) or not dsl.addPoint(point2):
            break
        else:
            print "added %s and %s" % (point1, point2)
    return result, ffmi.gi_frame.f_locals["i"]-index

def offset(freemanCodes, index, closed = True):
    fc = searchForwardQuadrant(freemanCodes, index, closed)
    bc = searchBackwardQuadrant(freemanCodes, index, closed)
    assert type(fc) == tuple and type(bc) == tuple
    if fc != bc:
        print "---"
        return Vector2(0, 0)
    dsl, ofs = tangentDSL(freemanCodes, index, closed)
    #dsl.convert8to4()
    alpha = (2.*dsl.pos+dsl.b-1)/(2*dsl.b)
    q = quadrant(*fc)
    if q == 0:
        print ofs, index, fc, bc, dsl
        return Vector2( alpha, -alpha)
    elif True:
        return Vector2(0, 0)
    elif q == 1:
        return Vector2(-alpha, -alpha)
    elif q == 2:
        return Vector2(-alpha,  alpha)
    else:
        return Vector2( alpha,  alpha)

import Gnuplot
class DSLExperiment(object):
    def __init__(self, reverse = False):
        g = Gnuplot.Gnuplot()
        g("set xtics 1; set ytics 1; set grid xtics ytics")
        g("set size ratio -1")
        self.g = g
        self.dsl = None
        self.pos = Point2D(0, 0)
        self.points = [self.pos]
        self.code1 = None
        self.code2 = None
        self.freeman2Diff = reverse and freeman2Diff8ConnNeg or freeman2Diff8Conn
    
    def __call__(self, code):
        if self.dsl == None:
            self.dsl = DigitalStraightLine(code % 2 and 1 or 0, 1, 0)
            self.code1 = code
        if code != self.code1:
            if self.code2 == None:
                self.code2 = code
            assert code == self.code2, "more than two different freeman codes passed to DigitalStraightLine!"
        newPos = self.pos + self.freeman2Diff[code % 2]
        if self.dsl.addPoint(newPos):
            self.pos = newPos
            self.points.append(newPos)
            self.plot()
            return True
        return False
    
    def plot(self):
        for point in self.points:
            if not self.dsl.contains(point):
                print "%s lost (no longer in DSL!)" % point
        self.g.plot(Gnuplot.Func(self.dsl.plotEquation(), title = "center line"),
                    Gnuplot.Func(self.dsl.plotEquation(1), title = "lower leaning line"),
                    Gnuplot.Func(self.dsl.plotEquation(2), title = "upper leaning line"),
                    self.points)

#     si = index
#     forwardCodes = []
#     while freemanCodes[si] in fc:
#         si -= 1

if __name__ == "__main__":
    dsl = DigitalStraightLine(0, 1, 0)
    dsl.addPoint(Point2D( 1,  0))
    dsl.addPoint(Point2D(-1,  0))
    dsl.addPoint(Point2D(-2, -1))

