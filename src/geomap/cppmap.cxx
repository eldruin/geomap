#include <boost/python.hpp>
#include <boost/python/detail/api_placeholder.hpp>
#include <boost/shared_ptr.hpp>
#include <vigra/tinyvector.hxx>
#include <vigra/pythonimage.hxx>
#include "vigra/polygon.hxx"
#include "vigra/positionedmap.hxx"
#include <vector>
#include <iostream>
#include "filteriterator.hxx"

namespace bp = boost::python;

typedef vigra::TinyVector<double, 2> Vector2;
typedef vigra::PointArray<Vector2>   Vector2Array;
typedef vigra::BBoxPolygon<vigra::Vector2> Polygon;

//#define USE_INSECURE_CELL_PTRS

#ifdef USE_INSECURE_CELL_PTRS
#define CELL_PTR(Type) Type *
#define NULL_PTR(Type) (Type *)NULL
#define RESET_PTR(ptr) ptr = NULL
// This is quite dangerous, *but*: The real lifetime of
// the referenced objects / cells are unknown, since any Euler
// operation might invalidate them.
#define CELL_RETURN_POLICY return_value_policy<reference_existing_object>
#else
#define CELL_PTR(Type) boost::shared_ptr<Type>
#define NULL_PTR(Type) boost::shared_ptr<Type>()
#define RESET_PTR(ptr) ptr.reset()
#define CELL_RETURN_POLICY default_call_policies
#endif

typedef unsigned int CellLabel;

template<class POINTER>
struct NotNull
{
    bool operator()(const POINTER &p) const
    {
        return p.get();
    }
};

class GeoMap
{
  public:
    class Node;
    class Edge;
    class Face;
    class Dart;

    typedef std::vector< CELL_PTR(Node) > Nodes;
    typedef std::vector< CELL_PTR(Edge) > Edges;
    typedef std::vector< CELL_PTR(Face) > Faces;

    typedef vigra::FilterIterator<Nodes::iterator, NotNull<Nodes::value_type> >
        NodeIterator;
    typedef vigra::FilterIterator<Edges::iterator, NotNull<Edges::value_type> >
        EdgeIterator;
    typedef vigra::FilterIterator<Faces::iterator, NotNull<Faces::value_type> >
        FaceIterator;

  protected:
    Nodes nodes_;
    Edges edges_;
    Faces faces_;

    unsigned int nodeCount_;
    unsigned int edgeCount_;
    unsigned int faceCount_;

    PositionedMap nodeMap_;

    vigra::Size2D imageSize_;

  public:
    GeoMap(bp::list nodePositions,
           bp::list edgeTuples, vigra::Size2D imageSize);

    ~GeoMap();

    NodeIterator nodesBegin()
        { return NodeIterator(nodes_.begin(), nodes_.end()); }
    NodeIterator nodesEnd()
        { return NodeIterator(nodes_.end(), nodes_.end()); }
    CELL_PTR(Node) node(CellLabel label)
    {
        return nodes_[label];
    }

    EdgeIterator edgesBegin()
        { return EdgeIterator(edges_.begin(), edges_.end()); }
    EdgeIterator edgesEnd()
        { return EdgeIterator(edges_.end(), edges_.end()); }
    CELL_PTR(Edge) edge(CellLabel label)
    {
        return edges_[label];
    }

    FaceIterator facesBegin()
        { return FaceIterator(faces_.begin(), faces_.end()); }
    FaceIterator facesEnd()
        { return FaceIterator(faces_.end(), faces_.end()); }
    CELL_PTR(Face) face(CellLabel label)
    {
        return faces_[label];
    }

    CellLabel nodeCount() const { return nodeCount_; }
    CellLabel maxNodeLabel() const { return nodes_.size(); }
    CellLabel edgeCount() const { return edgeCount_; }
    CellLabel maxEdgeLabel() const { return edges_.size(); }
    CellLabel faceCount() const { return faceCount_; }
    CellLabel maxFaceLabel() const { return faces_.size(); }

    const vigra::Size2D &imageSize() const
    {
        return imageSize_;
    }
};

class GeoMap::Node
{
  protected:
    typedef std::vector<int> DartLabels;

    GeoMap        *map_;
    CellLabel      label_;
    vigra::Vector2 position_;
    DartLabels     darts_;

    friend class Dart; // give access to darts_

  public:
    Node(GeoMap *map, const vigra::Vector2 &position)
    : map_(map),
      label_(map->nodes_.size()),
      position_(position)
    {
        map_->nodes_.push_back(GeoMap::Nodes::value_type(this));
        ++map_->nodeCount_;
        map_->nodeMap_.insert(position_, bp::object(label_));
    }

    ~Node()
    {
        RESET_PTR(map_->nodes_[label_]);
        --map_->nodeCount_;
        map_->nodeMap_.remove(position_);
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

    CellLabel degree() const
    {
        return darts_.size();
    }
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

  public:
    template<class POINTS>
    Edge(GeoMap *map, CellLabel startNodeLabel, CellLabel endNodeLabel,
         const POINTS &p)
    : Base(p),
      map_(map),
      label_(map->edges_.size()),
      startNodeLabel_(startNodeLabel),
      endNodeLabel_(endNodeLabel),
      leftFaceLabel_(0),
      rightFaceLabel_(0),
      protection_(0)
    {
        map_->edges_.push_back(GeoMap::Edges::value_type(this));
        ++map_->edgeCount_;
    }

    ~Edge()
    {
        RESET_PTR(map_->edges_[label_]);
        --map_->edgeCount_;
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
        return map_->node(startNodeLabel_);
    }

    CellLabel endNodeLabel() const
    {
        return endNodeLabel_;
    }

    GeoMap::Nodes::value_type endNode() const
    {
        return map_->node(endNodeLabel_);
    }

    CellLabel leftFaceLabel() const
    {
        return leftFaceLabel_;
    }

    GeoMap::Faces::value_type leftFace() const
    {
        return map_->face(leftFaceLabel_);
    }

    CellLabel rightFaceLabel() const
    {
        return rightFaceLabel_;
    }

    GeoMap::Faces::value_type rightFace() const
    {
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

  public:
    Dart(GeoMap *map, int label)
    : map_(map),
      label_(label)
    {}

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

//         def _setLeftFaceLabel(self, label):
//             if self.label() > 0:
//                 self.edge()._leftFaceLabel = label
//             else:
//                 self.edge()._rightFaceLabel = label

//         def edge(self):
//             """Returns corresponding edge or None if that edge has already
//             been removed."""
//             result = self._map().edge(self.edgeLabel())
//             return result

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

//         def __iter__(self):
//             if self._label > 0:
//                 return self.guaranteedEdge().__iter__()
//             else:
//                 return self.guaranteedEdge().__reviter__()

//         def __getitem__(self, index):
//             if self._label > 0:
//                 return self.guaranteedEdge()[index]
//             else:
//                 return self.guaranteedEdge()[-1-index]

//         def __len__(self):
//             return len(self.guaranteedEdge())

    Dart &nextAlpha()
    {
        label_ = -label_;
        return *this;
    }

    Dart &nextSigma(int times = 1)
    {
        Node::DartLabels &darts(startNode()->darts_);
        Node::DartLabels::size_type i = 0;
        for(; i < darts.size(); ++i)
            if(darts[i] == label_)
                break;
        vigra_precondition(i < darts.size(),
                           "Dart not attached to its startnode??");
        i = (i + times) % darts.size();
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

};

double contourArea(const GeoMap::Dart dart)
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

class GeoMap::Face
{
  public:
    typedef Edge::BoundingBox BoundingBox;

  protected:
    GeoMap           *map_;
    CellLabel         label_;
    std::vector<Dart> anchors_;
    BoundingBox       boundingBox_;
    bool              boundingBoxValid_;
    double            area_;
    bool              areaValid_;
    int               pixelArea_;

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

        // FIXME: infinite face had "None"-anchor in python!
        if(label_)
        {
            anchors_.push_back(anchor);

            for(; !anchor.leftFaceLabel(); anchor.nextPhi())
            {
                // don't calculate area on-the-fly here; we want to
                // exclude bridges from the area!
                anchor.setLeftFaceLabel(label_);
            }
        }
    }

    ~Face()
    {
        RESET_PTR(map_->faces_[label_]);
        --map_->faceCount_;
    }

    CellLabel label() const
    {
        return label_;
    }

    const BoundingBox &boundingBox()
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

    double area()
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
};

void GeoMap::Node::setPosition(const vigra::Vector2 &p)
{
    map_->nodeMap_.remove(position_);
    position_ = p;
    for(unsigned int i = 0; i < darts_.size(); ++i)
    {
        if(i > 0)
            (*map_->edge( i))[ 0] = p;
        else
            (*map_->edge(-i))[-1] = p;
    }
    map_->nodeMap_.insert(p, bp::object(label_));
}

inline GeoMap::Dart GeoMap::Node::anchor() const
{
    return Dart(map_, darts_[0]);
}

GeoMap::GeoMap(bp::list nodePositions,
               bp::list edgeTuples, vigra::Size2D imageSize)
: nodeCount_(0),
  edgeCount_(0),
  faceCount_(0),
  imageSize_(imageSize)
{
    for(int i = 0; i < len(nodePositions); ++i)
    {
        bp::extract<Vector2> ve(nodePositions[i]);
        if(ve.check())
            new Node(this, ve());
        else
            nodes_.push_back(NULL_PTR(Node));
    }

    for(int i = 0; i < len(edgeTuples); ++i)
    {
        bp::extract<bp::tuple> ete(edgeTuples[i]);
        if(ete.check())
        {
            bp::tuple edgeTuple(ete());
            bp::extract<Vector2Array> pe(edgeTuple[2]);
            if(!pe.check())
            {
                std::cerr << "why, oh why, do I have to die??\n";
                PyErr_SetString(PyExc_TypeError,
                    "GeoMap.__init__: edge geometry not convertable to Vector2Array");
                bp::throw_error_already_set();
            }
            new Edge(this,
                     bp::extract<CellLabel>(edgeTuple[0])(),
                     bp::extract<CellLabel>(edgeTuple[1])(),
                     pe());
        }
        else
            edges_.push_back(NULL_PTR(Edge));
    }
}

GeoMap::~GeoMap()
{
    // make sure these objects are deleted first
    // (their destructors access this map!)
    nodes_.clear();
    edges_.clear();
    faces_.clear();
}

template<class T>
T &returnSelf(T &v)
{
    return v;
}

template<class Iterator>
typename Iterator::value_type nextIterPos(Iterator &v)
{
    if(!v.inRange())
    {
        PyErr_SetString(PyExc_StopIteration, "cells iterator exhausted");
        bp::throw_error_already_set();
    }
    return *v++;
}

using namespace boost::python;

void defMap()
{
    {
        CELL_RETURN_POLICY crp;

        scope geoMap(
            class_<GeoMap>("GeoMap", init<
                           bp::list, bp::list, vigra::Size2D>())
            .def("node", &GeoMap::node, crp)
            .def("nodeIter", &GeoMap::nodesBegin)
            .def("edge", &GeoMap::edge, crp)
            .def("edgeIter", &GeoMap::edgesBegin)
            .def("face", &GeoMap::face, crp)
            .def("faceIter", &GeoMap::facesBegin)
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
            );

        // return_internal_reference is a simplification, since the
        // true owner would be the GeoMap object (however, the
        // iterator's lifetime is expected to be long enough):
        class_<GeoMap::NodeIterator>("NodeIterator")
            .def("__iter__", (GeoMap::NodeIterator &
                              (*)(GeoMap::NodeIterator &))&returnSelf,
                 return_internal_reference<>())
            .def("next", &nextIterPos<GeoMap::NodeIterator>, crp);

        class_<GeoMap::EdgeIterator>("EdgeIterator")
            .def("__iter__", (GeoMap::EdgeIterator &
                              (*)(GeoMap::EdgeIterator &))&returnSelf,
                 return_internal_reference<>())
            .def("next", &nextIterPos<GeoMap::EdgeIterator>, crp);

        class_<GeoMap::FaceIterator>("FaceIterator")
            .def("__iter__", (GeoMap::FaceIterator &
                              (*)(GeoMap::FaceIterator &))&returnSelf,
                 return_internal_reference<>())
            .def("next", &nextIterPos<GeoMap::FaceIterator>, crp);

        class_<GeoMap::Node>("Node", init<GeoMap *, const vigra::Vector2 &>())
            .def("label", &GeoMap::Node::label)
            .def("position", &GeoMap::Node::position,
                 return_value_policy<copy_const_reference>())
            .def("setPosition", &GeoMap::Node::setPosition)
            .def("degree", &GeoMap::Node::degree)
        ;

        class_<GeoMap::Edge, bases<Polygon> >("Edge", no_init)
            .def(init<GeoMap *, CellLabel, CellLabel, GeoMap::Edge::Base>())
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
        ;

        class_<GeoMap::Face>("Face", init<GeoMap *, GeoMap::Dart>())
            .def("label", &GeoMap::Face::label)
            .def("boundingBox", &GeoMap::Face::boundingBox,
                 return_value_policy<copy_const_reference>())
            .def("area", &GeoMap::Face::area)
        ;

        return_internal_reference<> rif;

        class_<GeoMap::Dart>("Dart", no_init)
            .def(init<GeoMap *, int>())
            .def("label", &GeoMap::Dart::label)
            .def("edgeLabel", &GeoMap::Dart::edgeLabel)
            .def("edge", &GeoMap::Dart::edge, crp)
            .def("startNodeLabel", &GeoMap::Dart::startNodeLabel)
            .def("startNode", &GeoMap::Dart::startNode, crp)
            .def("endNodeLabel", &GeoMap::Dart::endNodeLabel)
            .def("endNode", &GeoMap::Dart::endNode, crp)
            .def("leftFaceLabel", &GeoMap::Dart::leftFaceLabel)
            .def("leftFace", &GeoMap::Dart::leftFace, crp)
            .def("rightFaceLabel", &GeoMap::Dart::rightFaceLabel)
            .def("rightFace", &GeoMap::Dart::rightFace, crp)
            .def("nextAlpha", &GeoMap::Dart::nextAlpha, rif)
            .def("nextSigma", &GeoMap::Dart::nextSigma, (arg("times") = 1), rif)
            .def("prevSigma", &GeoMap::Dart::prevSigma, (arg("times") = 1), rif)
            .def("nextPhi", &GeoMap::Dart::nextPhi, rif)
            .def("prevPhi", &GeoMap::Dart::prevPhi, rif)
        ;

#ifndef USE_INSECURE_CELL_PTRS
        register_ptr_to_python< CELL_PTR(GeoMap::Node) >();
        register_ptr_to_python< CELL_PTR(GeoMap::Edge) >();
        register_ptr_to_python< CELL_PTR(GeoMap::Face) >();
#endif
    }

    def("contourArea", &contourArea);
}

