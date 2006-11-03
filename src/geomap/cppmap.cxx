#include "cppmap.hxx"
#include <boost/python/detail/api_placeholder.hpp>
#include <vigra/tinyvector.hxx>
#include <vigra/pythonimage.hxx>
#include <vigra/pythonutil.hxx>
#include <iostream>
#include <algorithm>
#include <cmath>
#include "exporthelpers.hxx"

const CellLabel UNINITIALIZED_CELL_LABEL =
    vigra::NumericTraits<CellLabel>::max();

class GeoMap::Node
{
  protected:
    typedef std::vector<int> DartLabels;

    GeoMap        *map_;
    CellLabel      label_;
    vigra::Vector2 position_;
    DartLabels     darts_;

    friend class Dart; // give access to darts_
    friend class GeoMap; // give access to darts_ (add edge, sort edges, Euler..)

  public:
    Node(GeoMap *map, const vigra::Vector2 &position)
    : map_(map),
      label_(map->nodes_.size()),
      position_(position)
    {
        map_->nodes_.push_back(GeoMap::Nodes::value_type(this));
        ++map_->nodeCount_;
        map_->nodeMap_.insert(PositionedNodeLabel(position_, label_));
    }

    bool initialized() const
    {
        return map_ != NULL;
    }

    void uninitialize()
    {
        GeoMap *map = map_; // local copy (prevent 2nd uninitialize() through destructor)
        map_ = NULL;
        --map->nodeCount_;
        map->nodeMap_.erase(
            map->nodeMap_.nearest(PositionedNodeLabel(position_, label_),
                                  vigra::NumericTraits<double>::epsilon()));
        RESET_PTR(map->nodes_[label_]); // may have effect like "delete this;"
#ifdef USE_INSECURE_CELL_PTRS
        delete this;
#endif
    }

    CellLabel label() const
    {
        return label_;
    }

    const vigra::Vector2 &position() const
    {
        return position_;
    }

    void setPosition(const vigra::Vector2 &p);

    inline Dart anchor() const;

    unsigned int degree() const
    {
        return darts_.size();
    }

    inline bool operator==(const GeoMap::Node &other)
    {
        return label() == other.label() && map_ == other.map_;
    }

    inline bool operator!=(const GeoMap::Node &other)
    {
        return !operator==(other);
    }

    GeoMap *map() const
    {
        return map_;
    }

  private:
    Node(const Node &) {} // disallow copying
    Node &operator=(const Node &) { return *this; }
};

class GeoMap::Edge
: public vigra::BBoxPolygon<vigra::Vector2>
{
  public:
    typedef vigra::BBoxPolygon<vigra::Vector2> Base;

  protected:
    GeoMap   *map_;
    CellLabel label_;
    CellLabel startNodeLabel_, endNodeLabel_;
    CellLabel leftFaceLabel_, rightFaceLabel_;
    int       protection_;

    friend class Dart; // allow setLeftFaceLabel
    friend GeoMap::Edge &GeoMap::mergeEdges(Dart &);

  public:
    template<class POINTS>
    Edge(GeoMap *map, CellLabel startNodeLabel, CellLabel endNodeLabel,
         const POINTS &p)
    : Base(p),
      map_(map),
      label_(map->edges_.size()),
      startNodeLabel_(startNodeLabel),
      endNodeLabel_(endNodeLabel),
      leftFaceLabel_(UNINITIALIZED_CELL_LABEL),
      rightFaceLabel_(UNINITIALIZED_CELL_LABEL),
      protection_(0)
    {
        map_->edges_.push_back(GeoMap::Edges::value_type(this));
        ++map_->edgeCount_;
    }

    bool initialized() const
    {
        return map_ != NULL;
    }

    void uninitialize()
    {
        GeoMap *map = map_; // local copy (prevent 2nd uninitialize() through destructor)
        map_ = NULL;
        --map->edgeCount_;
        RESET_PTR(map->edges_[label_]); // may have effect like "delete this;"
#ifdef USE_INSECURE_CELL_PTRS
        delete this;
#endif
    }

    CellLabel label() const
    {
        return label_;
    }

    inline Dart dart() const;

    CellLabel startNodeLabel() const
    {
        return startNodeLabel_;
    }

    GeoMap::Nodes::value_type startNode() const
    {
        vigra_precondition(initialized(), "startNode() of uninitialized edge!");
        return map_->node(startNodeLabel_);
    }

    CellLabel endNodeLabel() const
    {
        return endNodeLabel_;
    }

    GeoMap::Nodes::value_type endNode() const
    {
        vigra_precondition(initialized(), "endNode() of uninitialized edge!");
        return map_->node(endNodeLabel_);
    }

    CellLabel leftFaceLabel() const
    {
        return leftFaceLabel_;
    }

    GeoMap::Faces::value_type leftFace() const
    {
        vigra_precondition(initialized(), "leftFace() of uninitialized edge!");
        return map_->face(leftFaceLabel_);
    }

    CellLabel rightFaceLabel() const
    {
        return rightFaceLabel_;
    }

    GeoMap::Faces::value_type rightFace() const
    {
        vigra_precondition(initialized(), "rightFace() of uninitialized edge!");
        return map_->face(rightFaceLabel_);
    }

    bool isBridge() const
    {
        return leftFaceLabel_ == rightFaceLabel_;
    }

    bool isLoop() const
    {
        return startNodeLabel_ == endNodeLabel_;
    }

    inline bool operator==(const GeoMap::Edge &other)
    {
        return label() == other.label() && map_ == other.map_;
    }

    inline bool operator!=(const GeoMap::Edge &other)
    {
        return !operator==(other);
    }

    GeoMap *map() const
    {
        return map_;
    }

  private:
    Edge(const Edge &) {} // disallow copying
    Edge &operator=(const Edge &) { return *this; }
};

class DartPointIter
{
    CELL_PTR(GeoMap::Edge) edge_;
    int index_, inc_, end_;

  public:
        /** the iterator's value type
        */
    typedef GeoMap::Edge::value_type value_type;

        /** the iterator's reference type (return type of <tt>*iter</tt>)
        */
    typedef value_type & reference;

        /** the iterator's pointer type (return type of <tt>operator-></tt>)
        */
    typedef value_type * pointer;

        /** the iterator tag (forward_iterator_tag)
        */
    typedef std::forward_iterator_tag iterator_category;

    DartPointIter(GeoMap::Dart const &dart);

    DartPointIter & operator++()
    {
        index_ += inc_;
        return *this;
    }

    DartPointIter operator++(int)
    {
        DartPointIter ret(*this);
        operator++();
        return ret;
    }

    /**
     * the opposite of inRange(); true if this iterator is behind the
     * range and should not be dereferenced any more
     */
    bool atEnd() const
    {
        return index_ == end_;
    }

    /**
     * the opposite of atEnd(); true if this iterator is dereferencable
     */
    bool inRange() const
    {
        return index_ != end_;
    }

    reference operator*() const
    {
        return (*edge_)[index_];
    }

    pointer operator->() const
    {
        return &(operator*());
    }
};

class GeoMap::Dart
{
  protected:
    GeoMap *map_;
    int     label_;

    void setLeftFaceLabel(CellLabel label)
    {
        if(label_ > 0)
            guaranteedEdge()->leftFaceLabel_ = label;
        else
            guaranteedEdge()->rightFaceLabel_ = label;
    }

    friend class Face; // allow setLeftFaceLabel in Face constructor
    friend GeoMap::Face &GeoMap::mergeFaces(Dart &);

  public:
    Dart(GeoMap *map, int label)
    : map_(map),
      label_(label)
    {}

    Dart clone() const
    {
        return Dart(map_, label_);
    }

    int label() const
    {
        return label_;
    }

    CellLabel edgeLabel() const
    {
        return abs(label_);
    }

    CellLabel startNodeLabel() const
    {
        if(label_ > 0)
            return guaranteedEdge()->startNodeLabel();
        else
            return guaranteedEdge()->endNodeLabel();
    }

    CellLabel endNodeLabel() const
    {
        if(label_ > 0)
            return guaranteedEdge()->endNodeLabel();
        else
            return guaranteedEdge()->startNodeLabel();
    }

//         def _setStartNode(self, node):
//             """changes corresponding start/end node of this dart's
//             edge and the first/last point of its' polygon, too"""
//             if self._label > 0:
//                 self.edge()._startNodeLabel = node._label
//                 self.edge()[0] = node.position()
//                 #self.edge().invalidateProperties()
//             else:
//                 self.edge()._endNodeLabel = node._label
//                 self.edge()[-1] = node.position()
//                 #self.edge().invalidateProperties()

    CellLabel leftFaceLabel() const
    {
        if(label_ > 0)
            return guaranteedEdge()->leftFaceLabel();
        else
            return guaranteedEdge()->rightFaceLabel();
    }

    CellLabel rightFaceLabel() const
    {
        if(label_ > 0)
            return guaranteedEdge()->rightFaceLabel();
        else
            return guaranteedEdge()->leftFaceLabel();
    }


    GeoMap::Edges::value_type edge() const
    {
        return map_->edge(edgeLabel());
    }

    GeoMap::Edges::value_type guaranteedEdge() const
    {
        GeoMap::Edges::value_type result(edge());
        vigra_precondition(result,
            "Cannot operate on invalid dart belonging to removed edge!");
        return result;
    }

    GeoMap::Nodes::value_type startNode() const
    {
        return map_->node(startNodeLabel());
    }

    GeoMap::Nodes::value_type endNode() const
    {
        return map_->node(endNodeLabel());
    }

    GeoMap::Faces::value_type leftFace() const
    {
        return map_->face(leftFaceLabel());
    }

    GeoMap::Faces::value_type rightFace() const
    {
        return map_->face(rightFaceLabel());
    }

    double partialArea() const
    {
        if(label_ > 0)
            return guaranteedEdge()->partialArea();
        else
            return -guaranteedEdge()->partialArea();
    }

    DartPointIter pointIter() const
    {
        return DartPointIter(*this);
    }

    typedef GeoMap::Edge::value_type value_type;

    const value_type &operator[](int index) const
    {
        if(label_ > 0)
            return (*guaranteedEdge())[index];
        else
            return (*guaranteedEdge())[size()-1-index];
    }

    GeoMap::Edge::size_type size() const
    {
        return guaranteedEdge()->size();
    }

    Dart &nextAlpha()
    {
        label_ = -label_;
        return *this;
    }

    Dart &nextSigma(int times = 1)
    {
        Node::DartLabels &darts(startNode()->darts_);
        int i = 0;
        for(; i < (int)darts.size(); ++i)
            if(darts[i] == label_)
                break;
        vigra_precondition(i < (int)darts.size(),
                           "Dart not attached to its startnode??");
        i = (i + times) % (int)darts.size();
        if(i < 0)
            i += darts.size();
        label_ = darts[i];
        return *this;
    }

    Dart &prevSigma(int times = 1)
    {
        return nextSigma(-times);
    }

    Dart &nextPhi()
    {
        return nextAlpha().prevSigma();
    }

    Dart &prevPhi()
    {
        return nextSigma().nextAlpha();
    }

    bool operator==(const Dart &other) const
    {
        return label_ == other.label_;
    }

    bool operator!=(const Dart &other) const
    {
        return label_ != other.label_;
    }

//         def alphaOrbit(self):
//             return self._orbit("nextAlpha")

//         def sigmaOrbit(self):
//             return self._orbit("nextSigma")

//         def phiOrbit(self):
//             return self._orbit("nextPhi")

//         def _orbit(self, opName):
//             dart = self.clone()
//             op = getattr(dart, opName)
//             while True:
//                 yield dart.clone()
//                 op()
//                 if dart == self:
//                     break

    GeoMap *map() const
    {
        return map_;
    }
};

DartPointIter::DartPointIter(GeoMap::Dart const &dart)
: edge_(dart.guaranteedEdge())
{
    if(dart.label() > 0)
    {
        index_ = 0;
        inc_ = 1;
        end_ = dart.size();
    }
    else
    {
        index_ = dart.size() - 1;
        inc_ = -1;
        end_ = -1;
    }
}

/*
 * Note: This code is based on the assumption that a dart must always
 * have a least two points!
 */
class ContourPointIter
{
    DartPointIter dpi_;
    GeoMap::Dart dart_, end_;

  public:
        /** the iterator's value type
        */
    typedef GeoMap::Edge::value_type value_type;

        /** the iterator's reference type (return type of <tt>*iter</tt>)
        */
    typedef value_type & reference;

        /** the iterator's pointer type (return type of <tt>operator-></tt>)
        */
    typedef value_type * pointer;

        /** the iterator tag (forward_iterator_tag)
        */
    typedef std::forward_iterator_tag iterator_category;

    ContourPointIter(GeoMap::Dart const &dart, bool firstTwice = false)
    : dpi_(dart),
      dart_(dart),
      end_(dart)
    {
        if(!firstTwice)
            ++dpi_;
    }

    ContourPointIter & operator++()
    {
        ++dpi_;
        if(dpi_.atEnd())
        {
            if(dart_.nextPhi() != end_)
            {
                dpi_ = DartPointIter(dart_);
                ++dpi_;
            }
        }
        return *this;
    }

    ContourPointIter operator++(int)
    {
        ContourPointIter ret(*this);
        operator++();
        return ret;
    }

    /**
     * the opposite of inRange(); true if this iterator is behind the
     * range and should not be dereferenced any more
     */
    bool atEnd() const
    {
        return dpi_.atEnd();
    }

    /**
     * the opposite of atEnd(); true if this iterator is dereferencable
     */
    bool inRange() const
    {
        return dpi_.inRange();
    }

    reference operator*() const
    {
        return *dpi_;
    }

    pointer operator->() const
    {
        return &(operator*());
    }
};

double contourArea(const GeoMap::Dart &dart)
{
    double result = 0.0;
    GeoMap::Dart d(dart);
    do
    {
        if(!d.guaranteedEdge()->isBridge())
            result += d.partialArea();
    }
    while(d.nextPhi() != dart);
    return result;
}

Polygon contourPoly(const GeoMap::Dart &dart)
{
    Polygon result;
    GeoMap::Dart d(dart);
    do
    {
        if(d.label() < 0)
        {
            Polygon rev(*d.edge());
            rev.reverse();
            result.extend(rev);
        }
        else
            result.extend(*d.edge());
    }
    while(d.nextPhi() != dart);
    return result;
}

typedef vigra::MultiArray<2, int>::difference_type IVector2;

inline IVector2 intVPos(const Vector2 &p)
{
    return IVector2((int)floor(p[0]+0.5), (int)floor(p[1]+0.5));
}

class GeoMap::Face
{
  public:
    typedef Edge::BoundingBox BoundingBox;
    typedef std::vector<Dart> Contours;
    typedef Contours::const_iterator ContourIterator;

  protected:
    GeoMap             *map_;
    CellLabel           label_;
    std::vector<Dart>   anchors_;
    mutable BoundingBox boundingBox_;
    mutable bool        boundingBoxValid_;
    mutable double      area_;
    mutable bool        areaValid_;
    int                 pixelArea_;

    friend class GeoMap; // give access to pixelArea_ and anchors_ (Euler ops...)

    unsigned int findComponentAnchor(const GeoMap::Dart &dart);

  public:
    Face(GeoMap *map, Dart anchor)
    : map_(map),
      label_(map->faces_.size()),
      boundingBoxValid_(false),
      areaValid_(false),
      pixelArea_(0)
    {
        map_->faces_.push_back(GeoMap::Faces::value_type(this));
        ++map_->faceCount_;

        if(label_)
        {
            anchors_.push_back(anchor);

            for(; anchor.leftFaceLabel() == UNINITIALIZED_CELL_LABEL;
                anchor.nextPhi())
            {
                // don't calculate area on-the-fly here; we want to
                // exclude bridges from the area!
                anchor.setLeftFaceLabel(label_);
            }
        }
    }

    bool initialized() const
    {
        return map_ != NULL;
    }

    void uninitialize()
    {
        GeoMap *map = map_; // local copy (prevent 2nd uninitialize() through destructor)
        map_ = NULL;
        --map->faceCount_;
        RESET_PTR(map->faces_[label_]); // may have effect like "delete this;"
#ifdef USE_INSECURE_CELL_PTRS
        delete this;
#endif
    }

    CellLabel label() const
    {
        return label_;
    }

    const BoundingBox &boundingBox() const
    {
        vigra_precondition(label_, "infinite face has no boundingBox()!");

        if(!boundingBoxValid_)
        {
            boundingBox_ = BoundingBox();
            Dart anchor(anchors_[0]), dart(anchor);
            do
            {
                boundingBox_ |= dart.edge()->boundingBox();
            }
            while(dart.nextPhi() != anchor);
            boundingBoxValid_ = true;
        }
        return boundingBox_;
    }

    bool contains(const Vector2 &point) const
    {
        vigra_precondition(initialized(), "contains() of uninitialized face!");
        if(map_->labelImage_)
        {
            int l = (*map_->labelImage_)[intVPos(point)];
            if(l > 0 && (map_->faceLabelLUT_[l] == label_))
                return true;
        }
        unsigned int i = 0;
        if(label_)
        {
            if(!boundingBox().contains(point))
                return false;
            if(!contourPoly(anchors_[0]).contains(point))
                return false;
            ++i;
        }
        for(; i < anchors_.size(); ++i)
            if(contourPoly(anchors_[i]).contains(point))
                return false;
        return true;
    }

    double area() const
    {
        if(!areaValid_)
        {
            area_ = 0.0;
            for(unsigned int i = 0; i < anchors_.size(); ++i)
            {
                area_ += contourArea(anchors_[i]);
            }
            areaValid_ = true;
        }
        return area_;
    }

    const Dart &contour(unsigned int index = 0)
    {
        return anchors_[index];
    }

    ContourIterator contoursBegin() const
    {
        return anchors_.begin();
    }

    ContourIterator contoursEnd() const
    {
        return anchors_.end();
    }

    void embedContour(const Dart &anchor)
    {
        anchors_.push_back(anchor);

        Dart dart(anchor); // we need a non-const reference
        for(; dart.leftFaceLabel() != label_; dart.nextPhi())
            dart.setLeftFaceLabel(label_);

        if(areaValid_)
            area_ += contourArea(dart);

        vigra_postcondition(dart == anchor,
                            "contour labeled partially?!");
    }

    inline bool operator==(const GeoMap::Face &other)
    {
        return label() == other.label() && map_ == other.map_;
    }

    inline bool operator!=(const GeoMap::Face &other)
    {
        return !operator==(other);
    }

    GeoMap *map() const
    {
        return map_;
    }

  private:
    Face(const Face &) {} // disallow copying
    Face &operator=(const Face &) { return *this; }
};

void GeoMap::Node::setPosition(const vigra::Vector2 &p)
{
    vigra_precondition(initialized(), "setPosition() of uninitialized node!");
    map_->nodeMap_.erase(
        map_->nodeMap_.nearest(PositionedNodeLabel(position_, label_),
                               vigra::NumericTraits<double>::epsilon()));
    position_ = p;
    for(unsigned int i = 0; i < darts_.size(); ++i)
    {
        if(i > 0)
        {
            (*map_->edge( i))[ 0] = p;
        }
        else
        {
            GeoMap::Edge &edge(*map_->edge(-i));
            edge[edge.size()-1] = p;
        }
    }
    map_->nodeMap_.insert(PositionedNodeLabel(p, label_));
}

inline GeoMap::Dart GeoMap::Node::anchor() const
{
    vigra_precondition(initialized(), "anchor() of uninitialized node!");
    return Dart(map_, darts_[0]);
}

GeoMap::GeoMap(bp::list nodePositions,
               bp::list edgeTuples, vigra::Size2D imageSize)
: nodeCount_(0),
  edgeCount_(0),
  faceCount_(0),
  imageSize_(imageSize),
  labelImage_(NULL)
{
    if(len(nodePositions))
        std::cerr << "initalizing nodes...\n";
    nodes_.push_back(NULL_PTR(Node));
    for(int i = 1; i < len(nodePositions); ++i)
    {
        bp::extract<Vector2> ve(nodePositions[i]);
        if(ve.check())
            addNode(ve());
        else
            nodes_.push_back(NULL_PTR(Node));
    }

    if(len(edgeTuples))
        std::cerr << "initalizing edges...\n";
    edges_.push_back(NULL_PTR(Edge));
    for(int i = 1; i < len(edgeTuples); ++i)
    {
        bp::extract<bp::tuple> ete(edgeTuples[i]);
        if(ete.check())
        {
            bp::tuple edgeTuple(ete());
            bp::extract<Vector2Array> pe(edgeTuple[2]);
            if(!pe.check())
            {
                std::cerr << "why, oh why, do I have to die??\n";
                bp::throw_type_error(
                    "GeoMap.__init__: edge geometry not convertable to Vector2Array");
            }
            CellLabel startNodeLabel = bp::extract<CellLabel>(edgeTuple[0])();
            CellLabel endNodeLabel   = bp::extract<CellLabel>(edgeTuple[1])();
            addEdge(startNodeLabel, endNodeLabel, pe());
        }
        else
            edges_.push_back(NULL_PTR(Edge));
    }

    if(edgeCount_)
    {
        std::cerr << "initalizing sigma orbits...\n";
        sortEdgesDirectly();
        std::cerr << "initalizing contours...\n";
        initContours();
        std::cerr << "initalizing faces (" << faceCount() << " contours)...\n";
        embedFaces();
    }
}

GeoMap::~GeoMap()
{
    // make sure the cells' destructors don't access this map!
    for(NodeIterator it = nodesBegin(); it.inRange(); ++it)
        (*it)->uninitialize();
    for(EdgeIterator it = edgesBegin(); it.inRange(); ++it)
        (*it)->uninitialize();
    for(FaceIterator it = facesBegin(); it.inRange(); ++it)
        (*it)->uninitialize();
}

inline GeoMap::Dart GeoMap::dart(int label)
{
    return GeoMap::Dart(this, label);
}

double angleTheta(double dy, double dx); // implemented in polygon.cxx

CELL_PTR(GeoMap::Face) GeoMap::faceAt(const vigra::Vector2 &position)
{
    if(labelImage_)
    {
        GeoMap::LabelImage::difference_type p(intVPos(position));
        if(labelImage_->isInside(p))
        {
            int faceLabel = (*labelImage_)[p];
            if(faceLabel > 0)
                return face(faceLabelLUT_[faceLabel]);
        }
    }

    FaceIterator it = facesBegin();
    for(++it; it.inRange(); ++it)
        if((*it)->contains(position))
            return *it;

    return face(0);
}

CELL_PTR(GeoMap::Node) GeoMap::addNode(
    const vigra::Vector2 &position)
{
    GeoMap::Node *result = new GeoMap::Node(this, position);
    return node(result->label());
}

CELL_PTR(GeoMap::Edge) GeoMap::addEdge(
    CellLabel startNodeLabel, CellLabel endNodeLabel,
    const Vector2Array &points, CellLabel label)
{
    if(label > edges_.size())
        edges_.resize(label, NULL_PTR(GeoMap::Edge));
    CELL_PTR(GeoMap::Node)
        startNode = node(startNodeLabel),
        endNode = node(endNodeLabel);
    vigra_precondition(
        startNode && endNode, "addEdge(): invalid start- or endNodeLabel!");
    GeoMap::Edge *result = new GeoMap::Edge(
        this, startNodeLabel, endNodeLabel, points);
    startNode->darts_.push_back( (int)result->label());
    endNode  ->darts_.push_back(-(int)result->label());
    return edge(result->label());
}

void GeoMap::sortEdgesDirectly()
{
    typedef std::pair<double, int> DartAngle;

    for(NodeIterator it = nodesBegin(); it.inRange(); ++it)
    {
        std::vector<DartAngle> dartAngles;

        GeoMap::Node::DartLabels &dartLabels((*it)->darts_);

        for(unsigned int i = 0; i < dartLabels.size(); ++i)
        {
            GeoMap::Dart d(dart(dartLabels[i]));
            vigra_precondition(
                d.size() >= 2, "cannot measure angle of darts with < 2 points!");
            dartAngles.push_back(
                DartAngle(angleTheta(-d[1][1] + d[0][1],
                                      d[1][0] - d[0][0]),
                          dartLabels[i]));
        }

        std::sort(dartAngles.begin(), dartAngles.end());

        for(unsigned int i = 0; i < dartLabels.size(); ++i)
        {
            dartLabels[i] = dartAngles[i].second;
        }
    }
}

class DartPosition
{
  public:
    DartPosition(const GeoMap::Dart &dart)
    : dart_(dart),
      hitEnd_(false),
      pointIter_(DartPointIter(dart)),
      segmentIndex_(0),
      arcLength_(0.0),
      partialLength_(0.0),
      position_(*pointIter_)
    {
        p1_ = *pointIter_;
        p2_ = *++pointIter_;
    }

    bool atEnd() const
    {
        return hitEnd_;
    }

    const vigra::Vector2 &operator()() const
    {
        return position_;
    }

    void leaveCircle(const vigra::Vector2 &center, double radius2)
    {
        while((p2_ - center).squaredMagnitude() < radius2)
            if(!nextSegment())
                break;

        position_ = p2_;
    }

    void intersectCircle(const vigra::Vector2 &center, double radius2)
    {
        while((p2_ - center).squaredMagnitude() < radius2)
        {
            if(!nextSegment())
            {
                partialLength_ = 0.0;
                position_ = p2_;
                return;
            }
        }

        vigra::Vector2 diff(p2_ - p1_);
        double dist2 = diff.squaredMagnitude();
        double lambda = (
            (std::sqrt(radius2 * dist2
                       - vigra::sq(p2_[0]*p1_[1] - p1_[0]*p2_[1]
                                   + center[0]*diff[1] - diff[0]*center[1]))
             - dot(diff, p1_ - center))
            / dist2);
        diff *= lambda;
        partialLength_ += diff.magnitude();
        position_ = p1_ + diff;
    }

    int dartLabel() const
    {
        return dart_.label();
    }

  protected:
    bool nextSegment()
    {
        //vigra_precondition(!hitEnd_, "DartPosition: trying to proceed from end");
        arcLength_ += (p2_ - p1_).magnitude();
        p1_ = p2_;
        ++pointIter_;
        if(pointIter_.atEnd())
        {
            hitEnd_ = true;
            return false;
        }
        p2_ = *pointIter_;
        ++segmentIndex_;
        return true;
    }

    GeoMap::Dart dart_;
    bool hitEnd_;
    DartPointIter pointIter_;
    unsigned int segmentIndex_;
    double arcLength_, partialLength_;
    vigra::Vector2 p1_, p2_, position_;
};

struct DartPositionAngle
{
    DartPosition dp;
    double absAngle, angle;

    DartPositionAngle(const GeoMap::Dart &dart)
    : dp(dart)
    {}

    bool operator<(const DartPositionAngle &other) const
    {
        return angle < other.angle;
    }
};

typedef std::vector<DartPositionAngle> DartPositionAngles;
typedef DartPositionAngles::iterator DPAI;

template<class Iterator>
void rotateArray(Iterator begin, Iterator newBegin, Iterator end)
{
    typedef std::vector<typename Iterator::value_type> TempArray;
    TempArray temp(begin, end);
    typename Iterator::difference_type pos(newBegin - begin);
    std::copy(temp.begin() + pos, temp.end(), begin);
    std::copy(temp.begin(), temp.begin() + pos, begin + (end - newBegin));
}

inline double normAngle(double diff)
{
    if(diff < -M_PI)
        diff += 2*M_PI;
    if(diff >= M_PI)
        diff -= 2*M_PI;
    return diff;
}

void sortEdgesInternal(const vigra::Vector2 &currentPos,
                       double referenceAngle,
                       DPAI dpBegin, DPAI dpEnd,
                       double stepDist2, double minAngle)
{
    if(dpEnd - dpBegin < 2)
        return;

    bool unsortableState = true;
    for(DPAI dpi = dpBegin; dpi != dpEnd; ++dpi)
    {
        if(!dpi->dp.atEnd())
        {
            unsortableState = false;
            dpi->dp.intersectCircle(currentPos, stepDist2);
            //dpi->dp.leaveCircle(currentPos, stepDist2);
        }

        dpi->absAngle =
            std::atan2(-dpi->dp()[1] + currentPos[1],
                        dpi->dp()[0] - currentPos[0]);

        dpi->angle = normAngle(dpi->absAngle - referenceAngle);
    }

    if(unsortableState)
    {
        vigra_fail("Unsortable group of edges occured and not handled yet!");
        return;
    }

    std::sort(dpBegin, dpEnd);

    // handle cyclicity of array first (by rotation if necessary):
    DPAI firstGroupStart = dpEnd;
    bool needRotation = false;
    while((--firstGroupStart)->angle + minAngle < dpBegin->angle)
    {
        needRotation = true;
        if(firstGroupStart == dpBegin)
        {
            needRotation = false; // whole array unsortable
            break;
        }
    }

    if(needRotation)
        rotateArray(dpBegin, firstGroupStart, dpEnd);

    // look for groups of parallel edges
    DPAI groupStart = dpBegin,
          groupLast = groupStart, // for convenience, this is always groupEnd - 1
           groupEnd = groupLast + 1;
    for(; true; ++groupLast, ++groupEnd)
    {
        // group ending?
        if((groupEnd == dpEnd) || // last group
           (groupEnd->angle >= groupLast->angle + minAngle)) // decision here
        {
            // recursion needed if > one dart in group:
            if(groupLast != groupStart)
            {
                // determine mean position of dart positions in subgroup:
                vigra::Vector2 meanPos(groupLast->dp());
                for(DPAI dpi = groupStart; dpi != groupLast; ++dpi)
                    meanPos += dpi->dp();
                meanPos /= (groupEnd - groupStart);

                // sort subgroup recursively:
                sortEdgesInternal(meanPos, normAngle(
                                      groupStart->absAngle +
                                      normAngle(groupLast->absAngle -
                                                groupStart->absAngle) / 2),
                                  groupStart, groupEnd,
                                  stepDist2, minAngle);
            }

            if(groupEnd == dpEnd)
                break; // loop end

            groupStart = groupEnd;
        }
    }
}

void GeoMap::sortEdgesEventually(double stepDist, double minDist)
{
    double minAngle = std::atan2(minDist, stepDist),
          stepDist2 = vigra::sq(stepDist);

    for(NodeIterator it = nodesBegin(); it.inRange(); ++it)
    {
        DartPositionAngles dartPositions;

        GeoMap::Node::DartLabels &dartLabels((*it)->darts_);

        for(unsigned int i = 0; i < dartLabels.size(); ++i)
            dartPositions.push_back(
                DartPositionAngle(dart(dartLabels[i])));

        sortEdgesInternal((*it)->position(), 0.0,
                          dartPositions.begin(), dartPositions.end(),
                          stepDist2, minAngle);

        for(unsigned int i = 0; i < dartLabels.size(); ++i)
        {
            dartLabels[i] = dartPositions[i].dp.dartLabel();
        }
    }
}

void GeoMap::initContours()
{
    vigra_precondition(faces_.size() == 0,
        "initContours() called when contours were already present");

    new Face(this, Dart(this, 0)); // create infinite face, dart will be ignored

    for(EdgeIterator it = edgesBegin(); it.inRange(); ++it)
    {
        if((*it)->leftFaceLabel() == UNINITIALIZED_CELL_LABEL)
            new Face(this, dart( (*it)->label()));
        if((*it)->rightFaceLabel() == UNINITIALIZED_CELL_LABEL)
            new Face(this, dart(-(*it)->label()));
    }
}

struct AbsAreaCompare
{
        // FIXME: actually, const pointers would suffice:
    bool operator()(CELL_PTR(GeoMap::Face) f1, CELL_PTR(GeoMap::Face) f2) const
    {
        double a1 = f1->area(), a2 = f2->area();
        double absdiff = fabs(a1) - fabs(a2);
        if(fabs(absdiff) < 1e-2 && ((a1 < 0) != (a2 < 0)))
            return (a1 < 0); // for faces with equal area, prefer the exterior one
        return absdiff > 0; // else, prefer face with larger absolute area
    }
};

void GeoMap::embedFaces(bool initLabelImage)
{
    vigra_precondition(!labelImage_,
        "embedFaces() called with already-initialized labelImage");

    if(initLabelImage)
    {
        labelImage_ = new LabelImage(
            LabelImage::size_type(imageSize().width(), imageSize().height()), 0);
        faceLabelLUT_.resize(faces_.size());
    }

    // copy and remove all preliminary contours except the infinite one:
    GeoMap::Faces contours(faces_.begin() + 1, faces_.end());
    std::sort(contours.begin(), contours.end(), AbsAreaCompare());
    std::fill(faces_.begin() + 1, faces_.end(), NULL_PTR(Face));

    for(unsigned int i = 0; i < contours.size(); ++i)
    {
        GeoMap::Face &contour(*contours[i]); // FIXME: const

        GeoMap::Dart anchor(contour.contour(0));

        bool isExterior = contour.area() <= 0;

        if(!isExterior)
        {
            faces_[contour.label()] = contours[i];

            if(initLabelImage)
            {
                std::auto_ptr<vigra::Scanlines> scanlines(
                    scanPoly(contourPoly(anchor), imageSize().height()));
                contour.pixelArea_ =
                    fillScannedPoly(*scanlines, (int)contour.label(),
                                    labelImage_->traverser_begin(),
                                    labelImage_->size(),
                                    vigra::StandardValueAccessor<int>());
                drawScannedPoly(*scanlines, -1,
                                labelImage_->traverser_begin(),
                                labelImage_->size(),
                                vigra::StandardValueAccessor<int>());
                faceLabelLUT_[contour.label()] = contour.label();
            }
        }
        else
        {
            // contour is a hole, determine parent face
            CELL_PTR(GeoMap::Face) parent = NULL_PTR(GeoMap::Face);

            if(initLabelImage)
            {
                ContourPointIter cpi(anchor);
                while(cpi.inRange())
                {
                    GeoMap::LabelImage::difference_type p(intVPos(*cpi++));
                    if(labelImage_->isInside(p))
                    {
                        int parentLabel = (*labelImage_)[p];
                        if(parentLabel >= 0)
                        {
                            parent = face(parentLabel);
                            break;
                        }
                    }
                }
            }

            if(!parent)
            {
                ContourPointIter cpi(anchor);
                while(cpi.inRange())
                {
                    for(FaceIterator it = facesBegin(); it.inRange(); ++it)
                    {
                        if((*it)->contains(*cpi++))
                        {
                            parent = *it;
                            goto parent_found; // double break
                        }
                    }
                }
            }

            if(!parent)
            {
                parent = face(0);
//                 vigra_postcondition(
//                     parent->contains(anchor[0]),
//                     "contour could not be embedded (parent not found)");
            }

        parent_found:
//             std::cerr << "  embedding contour " << contour.label()
//                       << " in face " << parent->label() << "\n";
            parent->embedContour(anchor);
            contour.uninitialize();
        }
    }
}

CELL_PTR(GeoMap::Node) GeoMap::nearestNode(
    const vigra::Vector2 &position,
    double maxSquaredDist)
{
    NodeMap::iterator n(
        nodeMap_.nearest(PositionedNodeLabel(position, 0), maxSquaredDist));
    if(n != nodeMap_.end())
        return node(n->second.payload);
    return NULL_PTR(GeoMap::Node);
}

bool GeoMap::checkConsistency()
{
    bool result = true;
    //std::cerr << "GeoMap[" << this << "].checkConsistency()\n";
    for(NodeIterator it = nodesBegin(); it.inRange(); ++it)
    {
        if((*it)->map() != this)
        {
            std::cerr << "  Node " << (*it)->label() << " has wrong map()!\n";
            result = false;
            break;
        }
    }
    for(EdgeIterator it = edgesBegin(); it.inRange(); ++it)
    {
        if((*it)->map() != this)
        {
            std::cerr << "  Edge " << (*it)->label() << " has wrong map()!\n";
            result = false;
            break;
        }
    }
    for(FaceIterator it = facesBegin(); it.inRange(); ++it)
    {
        if((*it)->map() != this)
        {
            std::cerr << "  Face " << (*it)->label() << " has wrong map()!\n";
            result = false;
            break;
        }
    }
    return result;
}

/********************************************************************/

#define shouldbeconst

class GeoMap::ModificationCallback
{
  public:
    virtual bool removeNode(GeoMap::Node &node);
    virtual bool preMergeEdges(shouldbeconst GeoMap::Dart &dart);
    virtual void postMergeEdges(GeoMap::Edge &survivor);
    virtual bool preRemoveBridge(shouldbeconst GeoMap::Dart &dart);
    virtual void postRemoveBridge(GeoMap::Face &survivor);
    virtual bool preMergeFaces(shouldbeconst GeoMap::Dart &dart);
    virtual void postMergeFaces(GeoMap::Face &survivor);
    virtual void associatePixels(GeoMap::Face &face, const PixelList &pixels);
};

bool GeoMap::ModificationCallback::removeNode(GeoMap::Node &node)
{
    return true;
}

bool GeoMap::ModificationCallback::preMergeEdges(GeoMap::Dart &dart)
{
    return true;
}

void GeoMap::ModificationCallback::postMergeEdges(GeoMap::Edge &)
{
}

bool GeoMap::ModificationCallback::preRemoveBridge(GeoMap::Dart &dart)
{
    return true;
}

void GeoMap::ModificationCallback::postRemoveBridge(GeoMap::Face &)
{
}

bool GeoMap::ModificationCallback::preMergeFaces(GeoMap::Dart &dart)
{
    return true;
}

void GeoMap::ModificationCallback::postMergeFaces(GeoMap::Face &)
{
}

void GeoMap::ModificationCallback::associatePixels(GeoMap::Face &,
                                                   const PixelList &)
{
}

/********************************************************************/

void GeoMap::removeIsolatedNode(GeoMap::Node &node)
{
    for(GeoMap::MCIterator it = removeNodeHooks_.begin();
        it != removeNodeHooks_.end(); ++it)
    {
        (*it)->removeNode(node);
    }

    node.uninitialize();
}

typedef vigra::MultiArray<2, int> LabelImage;

void rawAddEdgeToLabelImage(
    const vigra::Scanlines &scanlines, LabelImage &labelImage, int diff)
{
    // clip to image range vertically:
    int y = std::max(0, scanlines.startIndex()),
     endY = std::min(labelImage.size()[1], scanlines.endIndex());

    for(; y < endY; ++y)
    {
        const vigra::Scanlines::Scanline &scanline(scanlines[y]);
        for(unsigned int j = 0; j < scanline.size(); ++j)
        {
            // X range checking
            int begin = scanline[j].begin,
                  end = scanline[j].end;
            if(begin < 0)
                begin = 0;
            if(end > labelImage.size()[0])
                end = labelImage.size()[0];

            for(int x = begin; x < end; ++x)
                labelImage[LabelImage::difference_type(x, y)] += diff;
        }
    }
}

void removeEdgeFromLabelImage(
    const vigra::Scanlines &scanlines,
    LabelImage &labelImage,
    LabelImage::value_type substituteLabel,
    PixelList &outputPixels)
{
    // clip to image range vertically:
    int y = std::max(0, scanlines.startIndex()),
     endY = std::min(labelImage.size()[1], scanlines.endIndex());

    for(; y < endY; ++y)
    {
        const vigra::Scanlines::Scanline &scanline(scanlines[y]);
        for(unsigned int j = 0; j < scanline.size(); ++j)
        {
            // X range checking
            int begin = scanline[j].begin,
                  end = scanline[j].end;
            if(begin < 0)
                begin = 0;
            if(end > labelImage.size()[0])
                end = labelImage.size()[0];

            for(int x = begin; x < end; ++x)
            {
                LabelImage::reference old(labelImage(x, y));
                if(old != -1)
                {
                    old += 1;
                }
                else
                {
                    old = substituteLabel;
                    outputPixels.push_back(vigra::Point2D(x, y));
                }
            }
        }
    }
}

GeoMap::Edge &GeoMap::mergeEdges(GeoMap::Dart &dart)
{
    Dart d1(dart);
    d1.nextSigma();
    vigra_precondition(d1.edgeLabel() != dart.edgeLabel(),
                       "mergeEdges called on self-loop!");

    Dart d2(d1);
    d2.nextSigma();
    vigra_precondition(d2 == dart,
                       "mergeEdges cannot remove node with degree > 2!");

    vigra_assert(d1.leftFaceLabel() == d2.rightFaceLabel(),
                 "mergeEdges: broken map");
    vigra_assert(d2.leftFaceLabel() == d1.rightFaceLabel(),
                 "mergeEdges: broken map");

    GeoMap::Face *faces[2];
    faces[0] = dart.leftFace();
    faces[1] = dart.rightFace();
    for(GeoMap::Face *faceIt = faces; faceIt != faces+2; ++faceIt)
    {
        GeoMap::Face::Contours::iterator cEnd = (*faceIt)->anchors_.end();
        for(GeoMap::Face::Contours::iterator it = (*faceIt)->anchors_.begin();
            it != cEnd; ++it)
        {
            if(it->edgeLabel() == d2.edgeLabel())
            {
                it->nextPhi();
                break;
            }
        }
    }

    GeoMap::Node &mergedNode(*d1.startNode());
    GeoMap::Edge &survivor(*d1.edge());
    GeoMap::Edge &mergedEdge(*d2.edge());

    for(GeoMap::MCIterator it = mergeEdgesHooks_.begin();
        it != mergeEdgesHooks_.end(); ++it)
        vigra_precondition((*it)->preMergeEdges(d1),
                           "mergeEdges() cancelled by mergeEdges hook");
    for(GeoMap::MCIterator it = removeNodeHooks_.begin();
        it != removeNodeHooks_.end(); ++it)
        vigra_precondition((*it)->removeNode(mergedNode),
                           "mergeEdges() cancelled by removeNode hook");

    // TODO: history append?

    GeoMap::Dart d(d2);
    d.nextAlpha();
    GeoMap::Node &changedEndNode(*d.startNode());
    unsigned int cenDartIndex = 0;
    for(; cenDartIndex < changedEndNode.darts_.size(); ++cenDartIndex)
        if(changedEndNode.darts_[cenDartIndex] == d.label())
            break;
    vigra_postcondition(cenDartIndex < changedEndNode.darts_.size(),
                        "changing dart not found at node");

    if(labelImage_)
    {
        rawAddEdgeToLabelImage(*scanPoly(mergedEdge, labelImage_->size()[1]),
                               *labelImage_, -1);
        rawAddEdgeToLabelImage(*scanPoly(survivor, labelImage_->size()[1]),
                               *labelImage_, -1);
    }

    if(survivor.startNodeLabel() != mergedNode.label())
    {
        if(mergedEdge.startNodeLabel() != mergedNode.label())
            mergedEdge.reverse();
        survivor.extend(mergedEdge);
        survivor.endNodeLabel_ = changedEndNode.label();
    }
    else
    {
        survivor.reverse();
        if(mergedEdge.startNodeLabel() != mergedNode.label())
            mergedEdge.reverse();
        survivor.extend(mergedEdge);
        survivor.reverse();
        survivor.startNodeLabel_ = changedEndNode.label();
    }

    changedEndNode.darts_[cenDartIndex] = d1.label();

    if(labelImage_)
    {
        rawAddEdgeToLabelImage(*scanPoly(survivor, labelImage_->size()[1]),
                               *labelImage_, 1);
    }

    mergedNode.uninitialize();
    mergedEdge.uninitialize();

    for(GeoMap::MCIterator it = mergeEdgesHooks_.begin();
        it != mergeEdgesHooks_.end(); ++it)
        (*it)->postMergeEdges(survivor);

    return survivor;
}

unsigned int GeoMap::Face::findComponentAnchor(const GeoMap::Dart &dart)
{
    for(unsigned int i = 0; i < anchors_.size(); ++i)
        if(anchors_[i] == dart)
            return i;

    for(unsigned int i = 0; i < anchors_.size(); ++i)
    {
        GeoMap::Dart d(anchors_[i]);
        while(d.nextPhi() != anchors_[i])
            if(d == dart)
                return i;
    }

    vigra_fail("findComponentAnchor failed: dart not found in face contours!");
    return 42; // never reached
}

void GeoMap::associatePixels(GeoMap::Face &face, const PixelList &pixels)
{
    face.pixelArea_ += pixels.size();
//     for(unsigned int i = 0; i < pixels.size(); ++i)
//         pixelBounds_ |= pixels[i];
    for(GeoMap::MCIterator it = associatedPixelsHooks_.begin();
        it != associatedPixelsHooks_.end(); ++it)
        (*it)->associatePixels(face, pixels);
}

GeoMap::Face &GeoMap::removeBridge(GeoMap::Dart &dart)
{
    GeoMap::Edge &edge(*dart.edge());
    GeoMap::Face &face(*dart.leftFace());
    vigra_precondition(face.label() == dart.rightFace()->label(),
                       "removeBridge needs a bridge dart!");
    GeoMap::Node &node1(*dart.startNode());
    GeoMap::Node &node2(*dart.endNode());
    vigra_precondition(node1.label() != node2.label(),
                       "Inconsistent map: bridge to be removed is also a self-loop!?");

    for(GeoMap::MCIterator it = removeBridgeHooks_.begin();
        it != removeBridgeHooks_.end(); ++it)
        vigra_precondition((*it)->preRemoveBridge(dart),
                           "removeBridge() cancelled by hook");

    // TODO: history append?

    Dart newAnchor1(dart), newAnchor2(dart);
    newAnchor1.prevSigma();
    newAnchor2.nextAlpha().prevSigma();
    unsigned int contourIndex = face.findComponentAnchor(dart);

    node1.darts_.erase(std::find(node1.darts_.begin(),
                                 node1.darts_.end(),  dart.label()));
    node2.darts_.erase(std::find(node2.darts_.begin(),
                                 node2.darts_.end(), -dart.label()));

    if(contourIndex == 0)
    {
        // determine outer anchor, swap if necessary:
        if(newAnchor1.edgeLabel() == dart.edgeLabel() ||
           newAnchor2.edgeLabel() != dart.edgeLabel() &&
           contourArea(newAnchor1) < contourArea(newAnchor2))
            std::swap(newAnchor1, newAnchor2);
    }

    face.anchors_[contourIndex] = newAnchor1;
    face.anchors_.push_back(newAnchor2);

    PixelList associatedPixels;
    if(labelImage_)
        removeEdgeFromLabelImage(*scanPoly(edge, labelImage_->size()[1]),
                                 *labelImage_, face.label(), associatedPixels);

    // remove singular nodes
    if(newAnchor1.edgeLabel() == dart.edgeLabel())
    {
        removeIsolatedNode(*newAnchor1.startNode());
        face.anchors_.erase(face.anchors_.begin() + contourIndex);
    }
    if(newAnchor2.edgeLabel() == dart.edgeLabel())
    {
        removeIsolatedNode(*newAnchor2.startNode());
        face.anchors_.erase(face.anchors_.end() - 1);
    }

    edge.uninitialize();

    for(GeoMap::MCIterator it = removeBridgeHooks_.begin();
        it != removeBridgeHooks_.end(); ++it)
        (*it)->postRemoveBridge(face);

    if(associatedPixels.size())
        associatePixels(face, associatedPixels);

    return face;
}

GeoMap::Face &GeoMap::mergeFaces(GeoMap::Dart &dart)
{
    GeoMap::Dart removedDart(dart);

    if(dart.leftFace()->area() < dart.rightFace()->area())
        removedDart.nextAlpha();
    if(not removedDart.rightFaceLabel()) // face 0 shall stay face 0
        removedDart.nextAlpha();

    GeoMap::Edge &mergedEdge(*removedDart.edge());
    GeoMap::Face &survivor(*removedDart.leftFace());
    GeoMap::Face &mergedFace(*removedDart.rightFace());
    GeoMap::Node &node1(*removedDart.startNode());
    GeoMap::Node &node2(*removedDart.endNode());

    vigra_precondition(survivor.label() != mergedFace.label(),
                       "mergeFaces(): dart belongs to a bridge!");

    unsigned int contour1 = survivor.findComponentAnchor(removedDart);
    unsigned int contour2 = mergedFace.findComponentAnchor(
        GeoMap::Dart(removedDart).nextAlpha());

    for(GeoMap::MCIterator it = mergeFacesHooks_.begin();
        it != mergeFacesHooks_.end(); ++it)
        vigra_precondition((*it)->preMergeFaces(dart),
                           "mergeFaces() cancelled by hook");

    // TODO: history append?

    // remember bounding box of merged face for later updating
    GeoMap::Face::BoundingBox mergedBBox;
    if(survivor.boundingBoxValid_)
        mergedBBox = mergedFace.boundingBox();

    // relabel contour's leftFaceLabel
    for(unsigned int i = 0; i < mergedFace.anchors_.size(); ++i)
    {
        GeoMap::Dart d(mergedFace.anchors_[i]);
        while(d.nextPhi().leftFaceLabel() != survivor.label())
            d.setLeftFaceLabel(survivor.label());
    }

    // re-use an old anchor for the merged contour
    if(survivor.anchors_[contour1].edgeLabel() == mergedEdge.label())
    {
        survivor.anchors_[contour1].nextPhi();
        if(survivor.anchors_[contour1].edgeLabel() == mergedEdge.label())
        {
            survivor.anchors_[contour1] = mergedFace.anchors_[contour2];
            if(survivor.anchors_[contour1].edgeLabel() == mergedEdge.label())
                survivor.anchors_[contour1].nextPhi();
        }
    }

    // check validity of found anchor
    if(survivor.anchors_[contour1].edgeLabel() == mergedEdge.label())
    {
        vigra_precondition(node1.label() == node2.label(),
                           "special-case: merging a self-loop");
        // results in an isolated node:
        survivor.anchors_.erase(survivor.anchors_.begin() + contour1);
    }

    // copy all remaining anchors into survivor's list:
    for(unsigned int i = 0; i < mergedFace.anchors_.size(); ++i)
    {
        if(i != contour2)
            survivor.anchors_.push_back(mergedFace.anchors_[i]);
    }

    // relabel region in image
    PixelList associatedPixels;
    if(labelImage_)
    {
//         relabelImage(map.labelImage.subImage(mergedFace.pixelBounds_),
//                      mergedFace.label(), survivor.label())
        for(unsigned int i = 0; i < faceLabelLUT_.size(); ++i)
            if(faceLabelLUT_[i] == mergedFace.label())
                faceLabelLUT_[i] = survivor.label();

        removeEdgeFromLabelImage(
            *scanPoly(mergedEdge, labelImage_->size()[1]),
            *labelImage_, survivor.label(), associatedPixels);

//         survivor.pixelBounds_ |= mergedFace.pixelBounds_;
    }

    node1.darts_.erase(std::find(node1.darts_.begin(),
                                 node1.darts_.end(),  removedDart.label()));
    node2.darts_.erase(std::find(node2.darts_.begin(),
                                 node2.darts_.end(), -removedDart.label()));

    // remove singular nodes
    bool removeNode1 = !node1.degree();
    if(!node2.degree() && node2.label() != node1.label())
        removeIsolatedNode(node2);
    if(removeNode1)
        removeIsolatedNode(node1);

    if(survivor.areaValid_)
        survivor.area_ += mergedFace.area();
    survivor.pixelArea_ += mergedFace.pixelArea_;

    if(survivor.boundingBoxValid_)
        survivor.boundingBox_ |= mergedBBox;

    mergedEdge.uninitialize();
    mergedFace.uninitialize();

    for(GeoMap::MCIterator it = mergeFacesHooks_.begin();
        it != mergeFacesHooks_.end(); ++it)
        (*it)->postMergeFaces(survivor);

    if(associatedPixels.size())
        associatePixels(survivor, associatedPixels);

    return survivor;
}

/********************************************************************/

void removeIsolatedNode(GeoMap::Node &node)
{
    node.map()->removeIsolatedNode(node);
}

CELL_PTR(GeoMap::Edge) mergeEdges(GeoMap::Dart &dart)
{
    GeoMap::Edge &survivor(dart.map()->mergeEdges(dart));
    return dart.map()->edge(survivor.label());
}

CELL_PTR(GeoMap::Face) removeBridge(GeoMap::Dart &dart)
{
    GeoMap::Face &survivor(dart.map()->removeBridge(dart));
    return dart.map()->face(survivor.label());
}

CELL_PTR(GeoMap::Face) mergeFaces(GeoMap::Dart &dart)
{
    GeoMap::Face &survivor(dart.map()->mergeFaces(dart));
    return dart.map()->face(survivor.label());
}

/********************************************************************/

template<class Iterator>
struct RangeIterWrapper
: bp::class_<Iterator>
{
    RangeIterWrapper(const char *name)
    : bp::class_<Iterator>(name, bp::no_init)
    {
        def("__iter__", (Iterator &(*)(Iterator &))&returnSelf,
            bp::return_internal_reference<>());
        def("next", &nextIterPos, CELL_RETURN_POLICY());
    }

    static Iterator &returnSelf(Iterator &v)
    {
        return v;
    }

    static typename Iterator::value_type nextIterPos(Iterator &v)
    {
        if(!v.inRange())
        {
            PyErr_SetString(PyExc_StopIteration, "cells iterator exhausted");
            bp::throw_error_already_set();
        }
        return *v++;
    }

};

using namespace boost::python;

void defMap()
{
    CELL_RETURN_POLICY crp;

    {
        scope geoMap(
            class_<GeoMap, boost::noncopyable>(
                "GeoMap", init<bp::list, bp::list, vigra::Size2D>())
            .def("node", &GeoMap::node, crp)
            .def("nodeIter", &GeoMap::nodesBegin)
            .def("edge", &GeoMap::edge, crp)
            .def("edgeIter", &GeoMap::edgesBegin)
            .def("face", &GeoMap::face, crp)
            .def("faceIter", &GeoMap::facesBegin)
            .def("dart", &GeoMap::dart)
            .add_property("nodeCount", &GeoMap::nodeCount)
            .add_property("edgeCount", &GeoMap::edgeCount)
            .add_property("faceCount", &GeoMap::faceCount)
            .def("maxNodeLabel", &GeoMap::maxNodeLabel,
                 "Returns an upper bound on the node labels.\n"
                 "Actually, this is the max. node label + 1, so that you can use it as LUT size.")
            .def("maxEdgeLabel", &GeoMap::maxEdgeLabel,
                 "Returns an upper bound on the edge labels.\n"
                 "Actually, this is the max. edge label + 1, so that you can use it as LUT size.")
            .def("maxFaceLabel", &GeoMap::maxFaceLabel,
                 "Returns an upper bound on the face labels.\n"
                 "Actually, this is the max. face label + 1, so that you can use it as LUT size.")
            .def("imageSize", &GeoMap::imageSize,
                 return_value_policy<copy_const_reference>())
            .def("addNode", &GeoMap::addNode, crp,
                 args("position"))
            .def("addEdge", &GeoMap::addEdge, crp,
                 (arg("startNodeLabel"), arg("endNodeLabel"), arg("points"),
                  arg("label") = 0))
            .def("sortEdgesDirectly", &GeoMap::sortEdgesDirectly)
            .def("sortEdgesEventually", &GeoMap::sortEdgesEventually,
                 args("stepDist", "minDist"))
            .def("initContours", &GeoMap::initContours)
            .def("embedFaces", &GeoMap::embedFaces, (arg("initLabelImage") = true))
            .def("nearestNode", &GeoMap::nearestNode, crp,
                 (arg("position"), arg(
                     "maxSquaredDist") = vigra::NumericTraits<double>::max()))
            .def("checkConsistency", &GeoMap::checkConsistency)
            );

        RangeIterWrapper<GeoMap::NodeIterator>("NodeIterator");
        RangeIterWrapper<GeoMap::EdgeIterator>("EdgeIterator");
        RangeIterWrapper<GeoMap::FaceIterator>("FaceIterator");

        class_<GeoMap::Node, boost::noncopyable>("Node", init<GeoMap *, const vigra::Vector2 &>())
            .def("initialized", &GeoMap::Node::initialized)
            .def("label", &GeoMap::Node::label)
            .def("position", &GeoMap::Node::position,
                 return_value_policy<copy_const_reference>())
            .def("setPosition", &GeoMap::Node::setPosition)
            .def("degree", &GeoMap::Node::degree)
            .def("anchor", &GeoMap::Node::anchor)
            .def(self == self)
            .def(self != self)
        ;

        class_<GeoMap::Edge, bases<Polygon>, boost::noncopyable>("Edge", no_init)
            .def(init<GeoMap *, CellLabel, CellLabel, GeoMap::Edge::Base>())
            .def("initialized", &GeoMap::Edge::initialized)
            .def("label", &GeoMap::Edge::label)
            .def("startNodeLabel", &GeoMap::Edge::startNodeLabel)
            .def("startNode", &GeoMap::Edge::startNode, crp)
            .def("endNodeLabel", &GeoMap::Edge::endNodeLabel)
            .def("endNode", &GeoMap::Edge::endNode, crp)
            .def("leftFaceLabel", &GeoMap::Edge::leftFaceLabel)
            .def("leftFace", &GeoMap::Edge::leftFace, crp)
            .def("rightFaceLabel", &GeoMap::Edge::rightFaceLabel)
            .def("rightFace", &GeoMap::Edge::rightFace, crp)
            .def("isBridge", &GeoMap::Edge::isBridge)
            .def("isLoop", &GeoMap::Edge::isLoop)
            .def(self == self)
            .def(self != self)
        ;

        class_<GeoMap::Face, boost::noncopyable>("Face", init<GeoMap *, GeoMap::Dart>())
            .def("initialized", &GeoMap::Face::initialized)
            .def("label", &GeoMap::Face::label)
            .def("boundingBox", &GeoMap::Face::boundingBox,
                 return_value_policy<copy_const_reference>())
            .def("contains", &GeoMap::Face::contains)
            .def("area", &GeoMap::Face::area)
            .def("contour", &GeoMap::Face::contour,
                 return_internal_reference<>())
            .def(self == self)
            .def(self != self)
        ;

        return_internal_reference<> rself; // "return self" policy

        RangeIterWrapper<DartPointIter>("DartPointIter");

        class_<GeoMap::Dart>("Dart", no_init)
            .def(init<GeoMap *, int>())
            .def("clone", &GeoMap::Dart::clone)
            .def("label", &GeoMap::Dart::label)
            .def("edgeLabel", &GeoMap::Dart::edgeLabel)
            .def("edge", &GeoMap::Dart::edge, crp)
            .def("guaranteedEdge", &GeoMap::Dart::guaranteedEdge, crp)
            .def("startNodeLabel", &GeoMap::Dart::startNodeLabel)
            .def("startNode", &GeoMap::Dart::startNode, crp)
            .def("endNodeLabel", &GeoMap::Dart::endNodeLabel)
            .def("endNode", &GeoMap::Dart::endNode, crp)
            .def("leftFaceLabel", &GeoMap::Dart::leftFaceLabel)
            .def("leftFace", &GeoMap::Dart::leftFace, crp)
            .def("rightFaceLabel", &GeoMap::Dart::rightFaceLabel)
            .def("rightFace", &GeoMap::Dart::rightFace, crp)
            .def("partialArea", &GeoMap::Dart::partialArea, crp)
            .def("__getitem__", &Array__getitem__<GeoMap::Dart>)
            .def("__iter__", &GeoMap::Dart::pointIter)
            .def("__len__", &GeoMap::Dart::size)
            .def("nextAlpha", &GeoMap::Dart::nextAlpha, rself)
            .def("nextSigma", &GeoMap::Dart::nextSigma, (arg("times") = 1), rself)
            .def("prevSigma", &GeoMap::Dart::prevSigma, (arg("times") = 1), rself)
            .def("nextPhi", &GeoMap::Dart::nextPhi, rself)
            .def("prevPhi", &GeoMap::Dart::prevPhi, rself)
            .def(self == self)
            .def(self != self)
        ;

#ifndef USE_INSECURE_CELL_PTRS
        register_ptr_to_python< CELL_PTR(GeoMap::Node) >();
        register_ptr_to_python< CELL_PTR(GeoMap::Edge) >();
        register_ptr_to_python< CELL_PTR(GeoMap::Face) >();
#endif
    }

    def("removeIsolatedNode", &removeIsolatedNode);
    def("mergeEdges", &mergeEdges, crp);
    def("removeBridge", &removeBridge, crp);
    def("mergeFaces", &mergeFaces, crp);

    RangeIterWrapper<ContourPointIter>("ContourPointIter")
        .def(init<GeoMap::Dart, bool>((arg("dart"), arg("firstTwice") = false)));

    def("contourArea", &contourArea,
        "contourArea(anchor) -> float\n\n"
        "Returns the area of contourPoly(anchor) (is however much faster than\n"
        "using that function, since it simply sums up all partialArea()s of the\n"
        "darts in the phi orbit.");
    def("contourPoly", &contourPoly,
        "contourPoly(anchor) -> Polygon\n\n"
        "Returns a Polygon composed by traversing anchor's phi orbit once.");
}
