#ifndef VIGRA_FOUREIGHTSEGMENTATION_HXX
#define VIGRA_FOUREIGHTSEGMENTATION_HXX

#include "vigra/error.hxx"
#include "vigra/stdimage.hxx"
#include "vigra/stdimagefunctions.hxx"
#include "vigra/labelimage.hxx"
//#include "circulatoradaptor.hxx"
#include "celltypes.hxx"
#include "pixelneighborhood.hxx"
#include "contourcirculator.hxx"

namespace vigra {

namespace CellImage {

class FourEightSegmentation;

// ??? unsigned int
struct CellPixel
{
    CellType type_;
    int label_;

    CellPixel() {}
    CellPixel(CellType type, int label = 0)
        : type_(type), label_(label)
    {}

    inline CellType type() const { return type_; }
    inline void setType(CellType type) { type_ = type; }

    inline int label() const { return label_; }
    inline void setLabel(int label) { label_= label; }
    inline void setLabel(int label, CellType) { label_= label; }

    inline unsigned int id() const
        { return (unsigned int)label_ | (type_ << 30); }

    bool operator==(CellPixel const & rhs) const
        { return label_ == rhs.label_ && type_ == rhs.type_; }

    bool operator!=(CellPixel const & rhs) const
        { return label_ != rhs.label_ || type_ != rhs.type_; }
};

typedef BasicImage<CellPixel> CellImage;
typedef vigra::NeighborhoodCirculator<CellImage::Iterator, EightNeighborCode>
    CellImageEightCirculator;

struct CellImageTypeAccessor
{
    typedef CellType value_type;

    template<class Iterator>
    CellType operator()(const Iterator &it) const
    {
        return it->type();
    }

    template<class Iterator>
    void set(CellType type, const Iterator &it) const
    {
        it->setType(type);
    }
};

struct CellImageLabelAccessor
{
    typedef int value_type;

    template<class Iterator>
    int operator()(const Iterator &it) const
    {
        return it->label();
    }

    template<class Iterator>
    void set(int label, const Iterator &it) const
    {
        it->setLabel(label);
    }
};

template<CellType type>
struct CellImageLabelWriter
{
    typedef int value_type;

    template<class Iterator>
    void set(int label, const Iterator &it) const
    {
        it->setLabel(label, type);
    }
};

template<class VALUETYPE>
struct RelabelFunctor
{
    typedef VALUETYPE value_type;
    typedef VALUETYPE argument_type;
    typedef VALUETYPE result_type;

    RelabelFunctor(VALUETYPE oldValue, VALUETYPE newValue)
        : oldValue_(oldValue),
          newValue_(newValue)
    {}
    
    VALUETYPE operator()(VALUETYPE value) const
    {
        return (value == oldValue) ? newValue : value;
    }
    
    VALUETYPE oldValue_, newValue_;
};

// -------------------------------------------------------------------
//                            CellScanIterator
// -------------------------------------------------------------------
template<class CellImageIterator, class ImageIterator>
class CellScanIterator
{
    CellImageIterator cellUL_, cellLR_, cellIter_;
    typename CellImageIterator::value_type cellPixelValue_;
    ImageIterator imageIter_;
    int width_;

public:
        /** the iterator's value type
        */
    typedef typename ImageIterator::value_type value_type;

        /** the iterator's reference type (return type of <TT>*iter</TT>)
        */
    typedef typename ImageIterator::reference reference;

        /** the iterator's pointer type (return type of <TT>operator-></TT>)
        */
    typedef typename ImageIterator::pointer pointer;

        /** the iterator tag (forward_iterator_tag)
        */
    typedef std::forward_iterator_tag iterator_category;

    CellScanIterator()
    {}

    CellScanIterator(CellImageIterator cellUL, CellImageIterator cellLR,
                     typename CellImageIterator::value_type cellPixelValue,
                     ImageIterator imageIter)
        : cellUL_(cellUL), cellLR_(cellLR), cellIter_(cellUL),
          cellPixelValue_(cellPixelValue),
          imageIter_(imageIter),
          width_(cellLR.x - cellUL.x)
    {
        if(*cellIter_ != cellPixelValue_ && cellIter_ != cellLR_)
            ++(*this);
    }

    CellScanIterator & operator++()
    {
        if(cellIter_ != cellLR_)
        {
            ++cellIter_.x, ++imageIter_.x;
            while((cellIter_.x != cellLR_.x) && (*cellIter_ != cellPixelValue_))
				++cellIter_.x, ++imageIter_.x;

            if(cellIter_.x == cellLR_.x)
            {
                cellIter_.x -= width_, imageIter_.x -= width_;
                ++cellIter_.y, ++imageIter_.y;

                if(cellIter_.y != cellLR_.y)
                {
                    while(*cellIter_ != cellPixelValue_)
						++cellIter_.x, ++imageIter_.x;
                }
                else
                    cellIter_ = cellLR_;
             }
        }
        return *this;
    }

    CellScanIterator operator++(int)
    {
        CellScanIterator ret(*this);
        operator++();
        return ret;
    }

	bool isEnd() const
	{
		return cellIter_ == cellLR_;
	}

	bool operator==(CellScanIterator const &other) const
    {
        return cellIter_ == other.cellIter_;
    }

    bool operator!=(CellScanIterator const &other) const
    {
        return cellIter_ != other.cellIter_;
    }

    reference operator*() const
    {
        return *imageIter_;
    }

    pointer operator->() const
    {
        return imageIter_.operator->();
    }
};

// -------------------------------------------------------------------
//                             EdgelIterator
// -------------------------------------------------------------------
/**
 * An EdgelIterator starts walking in the direction of the Circulator
 * given on construction and walks along CellTypeLine type'ed
 * pixels. isEnd() will become true if the iterator steps on a
 * CellTypeVertex pixel or tries to pass one diagonally.
 *
 * It is used by the RayCirculator for jumpToOpposite() and in
 * labelEdge().
 */
class EdgelIterator
{
    CellImageEightCirculator neighborCirc_;
    bool isEnd_;

public:
    EdgelIterator(CellImageEightCirculator const & n)
        : neighborCirc_(n), isEnd_(false)
    {}

    CellImageEightCirculator::reference operator*() const
    {
        return *neighborCirc_;
    }

    CellImageEightCirculator::pointer operator->() const
    {
        return neighborCirc_.operator->();
    }

    bool isEnd() const
    {
        return isEnd_;
    }

    EdgelIterator & operator++()
    {
        neighborCirc_.moveCenterToNeighbor();
        neighborCirc_.turnRight();

        while(1)
        {
            if(neighborCirc_->type() == CellTypeVertex)
            {
                isEnd_ = true;
                break;
            }
            if(neighborCirc_->type() == CellTypeLine)
            {
                break;
            }
            ++neighborCirc_;
        }

        if(neighborCirc_.isDiagonal() &&
           neighborCirc_[1].type() == CellTypeVertex)
        {
            ++neighborCirc_;
            isEnd_ = true;
        }

        return *this;
    }

    EdgelIterator & jumpToOpposite()
    {
        while(!isEnd())
            operator++();
        neighborCirc_.swapCenterNeighbor();
        return *this;
    }

    operator CellImageEightCirculator()
    {
        return neighborCirc_;
    }
};

// -------------------------------------------------------------------
//                             RayCirculator
// -------------------------------------------------------------------
struct RayCirculator
{
private:
    CellImageEightCirculator neighborCirc_;
    FourEightSegmentation * segmentation_;
    bool isSingular_;

public:
    // this default constructor is needed for NodeInfo/EdgeInfo,
    // init() must not be called!
    RayCirculator() : segmentation_(0L) {}

    RayCirculator(FourEightSegmentation * segmentation,
                  CellImageEightCirculator const & circ)
        : neighborCirc_(circ),
          segmentation_(segmentation)
    {
        vigra_precondition(neighborCirc_.center()->type() == CellTypeVertex,
        "FourEightSegmentation::RayCirculator(): center is not a node");

        vigra_precondition(neighborCirc_->type() != CellTypeVertex,
        "FourEightSegmentation::RayCirculator(): neighbor is a node");

        CellImageEightCirculator n = neighborCirc_;
        isSingular_ = true;
        do
        {
            if(n->type() != CellTypeRegion)
            {
                isSingular_ = false;
                break;
            }
        }
        while(++n != neighborCirc_);

        if(neighborCirc_->type() != CellTypeLine)
            operator++();
    }

    RayCirculator & operator++()
    {
        if(isSingular_)
            return *this;

        tryNext();

        while(neighborCirc_->type() != CellTypeLine)
        {
            if(neighborCirc_->type() == CellTypeVertex)
            {
                neighborCirc_.swapCenterNeighbor();
            }
            tryNext();
        }
        return *this;
    }

    RayCirculator operator++(int)
    {
        RayCirculator ret(*this);
        operator++();
        return ret;
    }

    RayCirculator & operator--()
    {
        if(isSingular_)
            return *this;

        tryPrev();

        while(neighborCirc_->type() != CellTypeLine)
        {
            if(neighborCirc_->type() == CellTypeVertex)
            {
                neighborCirc_.swapCenterNeighbor();
            }
            tryPrev();
        }
        return *this;
    }

    RayCirculator operator--(int)
    {
        RayCirculator ret(*this);
        operator--();
        return ret;
    }

    RayCirculator & jumpToOpposite()
    {
        if(isSingular_)
            return *this;

        EdgelIterator line(neighborCirc_);
        line.jumpToOpposite();

        neighborCirc_ = line;

        return *this;
    }

    bool operator==(RayCirculator const & o) const
    {
        return neighborCirc_ == o.neighborCirc_;
    }

    bool operator!=(RayCirculator const & o) const
    {
        return neighborCirc_ != o.neighborCirc_;
    }

    FourEightSegmentation * segmentation() const
    {
        return segmentation_;
        //return neighborCirc_.segmentation();
    }

    CellImage::Iterator center() const { return neighborCirc_.center(); }

    int nodeLabel() const { return neighborCirc_.center()->label(); }
    int edgeLabel() const { return neighborCirc_->label(); }
    int leftFaceLabel() const { return neighborCirc_[1].label(); }
    int rightFaceLabel() const { return neighborCirc_[-1].label(); }

    inline int degree() const;
    inline float x() const;
    inline float y() const;

    const CellImageEightCirculator &neighborCirculator() const
    {
        return neighborCirc_;
    }

private:
    void tryNext()
    {
        ++neighborCirc_;

        if(badDiagonalConfig())
            ++neighborCirc_;
    }

    void tryPrev()
    {
        --neighborCirc_;

        if(badDiagonalConfig())
            --neighborCirc_;
    }

    // prevent double stop at a line pixel from different source
    // vertex pixels
    bool badDiagonalConfig()
    {
        return (neighborCirc_->type() == CellTypeLine &&
                (neighborCirc_[1].type() == CellTypeVertex ||
                 neighborCirc_[-1].type() == CellTypeVertex));
    }
};

// -------------------------------------------------------------------
//                           ContourCirculator
// -------------------------------------------------------------------
struct ContourCirculator
{
    RayCirculator ray_;

    ContourCirculator(RayCirculator r)
        : ray_(r)
    {}

    ContourCirculator & operator++()
    {
        ray_.jumpToOpposite();
        --ray_;
        return *this;
    }

    ContourCirculator operator++(int)
    {
        ContourCirculator ret(*this);
        operator++();
        return ret;
    }

    ContourCirculator & operator--()
    {
        ++ray_;
        ray_.jumpToOpposite();
        return *this;
    }

    ContourCirculator operator--(int)
    {
        ContourCirculator ret(*this);
        operator--();
        return ret;
    }

    ContourCirculator & jumpToOpposite()
    {
        ray_.jumpToOpposite();
        return *this;
    }

    bool operator==(ContourCirculator const & o) const
    {
        return ray_ == o.ray_;
    }

    bool operator!=(ContourCirculator const & o) const
    {
        return ray_ != o.ray_;
    }

    FourEightSegmentation * segmentation() const
    {
        return ray_.segmentation();
    }

    int nodeLabel() const { return ray_.nodeLabel(); }
    int edgeLabel() const { return ray_.edgeLabel(); }
    int leftFaceLabel() const { return ray_.leftFaceLabel(); }
    int rightFaceLabel() const { return ray_.rightFaceLabel(); }

    int degree() const { return ray_.degree(); }
    float x() const { return ray_.x(); }
    float y() const { return ray_.y(); }

    RayCirculator const & ray() const { return ray_; }
};

// -------------------------------------------------------------------
//                         FourEightSegmentation
// -------------------------------------------------------------------
class FourEightSegmentation
{
public:
    struct CellInfo
    {
        int label;
        Diff2D upperLeft, lowerRight;

        CellInfo() : label(-1) {}
        bool initialized() const { return label >= 0; }
    };

    struct NodeInfo : public CellInfo
    {
        float centerX, centerY;
        int size;
        int degree;
        RayCirculator ray;
    };

    struct EdgeInfo : public CellInfo
    {
        RayCirculator start, end;
    };

    struct FaceInfo : public CellInfo
    {
        Diff2D anchor;
        std::vector<ContourCirculator> contours;
    };

    typedef std::vector<NodeInfo> NodeList;
    typedef std::vector<EdgeInfo> EdgeList;
    typedef std::vector<FaceInfo> FaceList;

    typedef NodeList::iterator NodeIterator;
    typedef EdgeList::iterator EdgeIterator;
    typedef FaceList::iterator FaceIterator;

    typedef std::vector<ContourCirculator>::iterator BoundaryComponentsIterator;

    // -------------------------------------------------------------------
    //                  FourEightSegmentation::NodeAccessor
    // -------------------------------------------------------------------
    struct NodeAccessor
    {
        int degree(NodeIterator & i) const
        {
            return (*i).degree;
        }

        float x(NodeIterator & i) const
        {
            return (*i).centerX;
        }

        float y(NodeIterator & i) const
        {
            return (*i).centerY;
        }

        int label(NodeIterator & i) const
        {
            return (*i).label;
        }

        RayCirculator rayCirculator(NodeIterator & i) const
        {
            return (*i).ray;
        }
    };

    // -------------------------------------------------------------------
    //               FourEightSegmentation::NodeAtStartAccessor
    // -------------------------------------------------------------------
    struct NodeAtStartAccessor
    {
        int degree(RayCirculator & i) const
        {
            return i.degree();
        }

        int degree(ContourCirculator & i) const
        {
            return i.degree();
        }

        int degree(EdgeIterator & i) const
        {
            return degree((*i).start);
        }

        float x(RayCirculator & i) const
        {
            return i.x();
        }

        float y(RayCirculator & i) const
        {
            return i.y();
        }

        float x(ContourCirculator & i) const
        {
            return i.x();
        }

        float y(ContourCirculator & i) const
        {
            return i.y();
        }

        float x(EdgeIterator & i) const
        {
            return x((*i).start);
        }

        float y(EdgeIterator & i) const
        {
            return y((*i).start);
        }

        int label(RayCirculator & i) const
        {
            return i.nodeLabel();
        }

        int label(ContourCirculator & i) const
        {
            return i.nodeLabel();
        }

        int label(EdgeIterator & i) const
        {
            return label((*i).start);
        }

        RayCirculator rayCirculator(ContourCirculator & i) const
        {
            return i.ray();
        }

        RayCirculator rayCirculator(EdgeIterator & i) const
        {
            return (*i).start;
        }

        NodeIterator nodeIterator(RayCirculator & i) const
        {
            return i.segmentation()->nodeList.begin() + i.nodeLabel();
        }

        NodeIterator nodeIterator(ContourCirculator & i) const
        {
            return i.segmentation()->nodeList.begin() + i.nodeLabel();
        }

        NodeIterator nodeIterator(EdgeIterator & i) const
        {
            return nodeIterator((*i).start);
        }
    };

    // -------------------------------------------------------------------
    //                FourEightSegmentation::NodeAtEndAccessor
    // -------------------------------------------------------------------
    struct NodeAtEndAccessor
    {
        int degree(RayCirculator i) const
        {
            return i.jumpToOpposite().degree();
        }

        int degree(ContourCirculator i) const
        {
            return i.jumpToOpposite().degree();
        }

        int degree(EdgeIterator & i) const
        {
            return (*i).end.degree();
        }

        float x(RayCirculator i) const
        {
            return i.jumpToOpposite().x();
        }

        float y(RayCirculator i) const
        {
            return i.jumpToOpposite().y();
        }

        float x(ContourCirculator i) const
        {
            return i.jumpToOpposite().x();
        }

        float y(ContourCirculator i) const
        {
            return i.jumpToOpposite().y();
        }

        float x(EdgeIterator & i) const
        {
            return (*i).end.x();
        }

        float y(EdgeIterator & i) const
        {
            return (*i).end.y();
        }

        int label(RayCirculator i) const
        {
            return i.jumpToOpposite().nodeLabel();
        }

        int label(ContourCirculator i) const
        {
            return i.jumpToOpposite().nodeLabel();
        }

        int label(EdgeIterator & i) const
        {
            return (*i).end.nodeLabel();
        }

        RayCirculator rayCirculator(EdgeIterator & i) const
        {
            return (*i).end;
        }

        NodeIterator nodeIterator(RayCirculator & i) const
        {
            return i.segmentation()->nodeList.begin() + label(i);
        }

        NodeIterator nodeIterator(ContourCirculator & i) const
        {
            return i.segmentation()->nodeList.begin() + label(i);
        }

        NodeIterator nodeIterator(EdgeIterator & i) const
        {
            return (*i).end.segmentation()->nodeList.begin() + label(i);
        }
    };

    // -------------------------------------------------------------------
    //                  FourEightSegmentation::EdgeAccessor
    // -------------------------------------------------------------------
    struct EdgeAccessor
    {
        int label(RayCirculator & i) const
        {
            return i.edgeLabel();
        }

        int label(ContourCirculator & i) const
        {
            return i.edgeLabel();
        }

        int label(EdgeIterator & i) const
        {
            return (*i).label;
        }
    };

    // -------------------------------------------------------------------
    //                  FourEightSegmentation::FaceAccessor
    // -------------------------------------------------------------------
    struct FaceAccessor
    {
        int label(FaceIterator & i) const
        {
            return (*i).label;
        }

        int countBoundaryComponents(FaceIterator & i) const
        {
            return (*i).contours.size();
        }

        BoundaryComponentsIterator beginBoundaryComponentsIterator(FaceIterator & i) const
        {
            return (*i).contours.begin();
        }

        BoundaryComponentsIterator endBoundaryComponentsIterator(FaceIterator & i) const
        {
            return (*i).contours.end();
        }

        ContourCirculator contourCirculator(BoundaryComponentsIterator & i) const
        {
            return *i;
        }
    };

    // -------------------------------------------------------------------
    //               FourEightSegmentation::FaceAtLeftAccessor
    // -------------------------------------------------------------------
    struct FaceAtLeftAccessor
    {
        int label(RayCirculator & i) const
        {
            return i.leftFaceLabel();
        }

        int label(ContourCirculator & i) const
        {
            return i.leftFaceLabel();
        }

        ContourCirculator contourCirculator(RayCirculator & i) const
        {
            return ContourCirculator(i);
        }

        ContourCirculator contourCirculator(EdgeIterator & i) const
        {
            return ContourCirculator((*i).start);
        }
    };

    // -------------------------------------------------------------------
    //               FourEightSegmentation::FaceAtRightAccessor
    // -------------------------------------------------------------------
    struct FaceAtRightAccessor
    {
        int label(RayCirculator & i) const
        {
            return i.rightFaceLabel();
        }

        int label(ContourCirculator & i) const
        {
            return i.rightFaceLabel();
        }

        ContourCirculator contourCirculator(RayCirculator & i) const
        {
            return ContourCirculator(i).jumpToOpposite();
        }

        ContourCirculator contourCirculator(EdgeIterator & i) const
        {
            return ContourCirculator((*i).end);
        }
    };

    // -------------------------------------------------------------------

public:
    template<class SrcIter, class SrcAcc>
    void init(SrcIter ul, SrcIter lr, SrcAcc src)
    {
        width_ = lr.x - ul.x;
        height_ = lr.y - ul.y;
        int totalwidth = width_ + 4;
        int totalheight = height_ + 4;

        nodeCount_ = edgeCount_ = faceCount_ = 0;

        cellImage.resize(totalwidth, totalheight);
        cellImage = CellPixel(CellTypeRegion, 0);

        cells = cellImage.upperLeft() + Diff2D(2,2);

        // extract contours in input image and put frame around them
        BImage contourImage(totalwidth, totalheight);
        initFourEightSegmentationContourImage(ul, lr, src, contourImage);

        initCellImage(contourImage);

        std::cerr << "FourEightSegmentation::label0Cells()\n";
        int maxNodeLabel = label0Cells();

        std::cerr << "FourEightSegmentation::label1Cells(maxNodeLabel= "
                  << maxNodeLabel << ")\n";
        int maxEdgeLabel = label1Cells(maxNodeLabel);

        std::cerr << "FourEightSegmentation::label2Cells()\n";
        int maxFaceLabel = label2Cells(contourImage);

        std::cerr << "FourEightSegmentation::labelCircles(maxNodeLabel= "
                  << maxNodeLabel << ", maxEdgeLabel= " << maxEdgeLabel << ")\n";
        labelCircles(maxNodeLabel, maxEdgeLabel);

        std::cerr << "FourEightSegmentation::initNodeList(maxNodeLabel= "
                  << maxNodeLabel << ")\n";
        initNodeList(maxNodeLabel);
        std::cerr << "FourEightSegmentation::initEdgeList(maxEdgeLabel= "
                  << maxEdgeLabel << ")\n";
        initEdgeList(maxEdgeLabel);
        std::cerr << "FourEightSegmentation::initFaceList(maxFaceLabel= "
                  << maxFaceLabel << ")\n";
        initFaceList(contourImage, maxFaceLabel);

        std::cerr << "FourEightSegmentation::initBoundingBoxes()\n";
        initBoundingBoxes(maxNodeLabel, maxEdgeLabel, maxFaceLabel);
    }

    template<class SrcIter, class SrcAcc>
    void init(triple<SrcIter, SrcIter, SrcAcc> src)
    {
        init(src.first, src.second, src.third);
    }

    int width() const { return width_; }
    int height() const { return height_; }

    // the fooCount()s tell how many fooList elements are initialized()
    int nodeCount() const { return nodeCount_; }
    int edgeCount() const { return edgeCount_; }
    int faceCount() const { return faceCount_; }

    template<class ImageIterator>
    inline CellScanIterator<CellImage::Iterator, ImageIterator>
    cellScanIterator(CellInfo cell, CellType cellType,
					 ImageIterator const &upperLeft);

public:
    CellImage cellImage;
    CellImage::Iterator cells;

    NodeList nodeList;
    EdgeList edgeList;
    FaceList faceList;

private:
    unsigned int nodeCount_, edgeCount_, faceCount_;

    void initCellImage(BImage & contourImage);
    int label0Cells();
    int label1Cells(int maxNodeLabel);
    int label2Cells(BImage & contourImage);
    void labelCircles(int & maxNodeLabel, int & maxEdgeLabel);

    void labelEdge(CellImageEightCirculator rayAtStart, int newLabel);

    void initNodeList(int maxNodeLabel);
    void initEdgeList(int maxEdgeLabel);
    void initFaceList(BImage & contourImage, int maxFaceLabel);
    void initBoundingBoxes(int maxNodeLabel, int maxEdgeLabel,
                           int maxFaceLabel);

private:
    int width_, height_;
};

// -------------------------------------------------------------------
//                        RayCirculator functions
// -------------------------------------------------------------------
inline int RayCirculator::degree() const
{
    return segmentation()->nodeList[nodeLabel()].degree;
}

inline float RayCirculator::x() const
{
    return segmentation()->nodeList[nodeLabel()].centerX;
}

inline float RayCirculator::y() const
{
    return segmentation()->nodeList[nodeLabel()].centerY;
}

// -------------------------------------------------------------------
//                    FourEightSegmentation functions
// -------------------------------------------------------------------
template<class ImageIterator>
CellScanIterator<CellImage::Iterator, ImageIterator>
FourEightSegmentation::cellScanIterator(
    CellInfo cell, CellType cellType, ImageIterator const &upperLeft)
{
    return CellScanIterator<CellImage::Iterator, ImageIterator>
        (cells + cell.upperLeft, cells + cell.lowerRight,
         CellPixel(cellType, cell.label),
         upperLeft + cell.upperLeft);
}

template<class SrcIter, class SrcAcc>
void initFourEightSegmentationContourImage(SrcIter ul, SrcIter lr, SrcAcc src,
                                           BImage & contourImage)
{
    int w = lr.x - ul.x;
    int h = lr.y - ul.y;
    int x,y;

    initImageBorder(destImageRange(contourImage),
                    1, 0);
    initImageBorder(srcIterRange(contourImage.upperLeft()+Diff2D(1,1),
                                 contourImage.lowerRight()-Diff2D(1,1),
                                 contourImage.accessor()),
                    1, 1);

    typedef typename SrcAcc::value_type SrcType;
    SrcType zero = NumericTraits<SrcType>::zero();
    for(y=0; y<h; ++y, ++ul.y)
    {
        SrcIter sx = ul;
        for(x=0; x<w; ++x, ++sx.x)
        {
            if(src(sx) == zero)
                contourImage(x+2, y+2) = 1;
        }
    }
}

void FourEightSegmentation::initCellImage(BImage & contourImage)
{
    BImage::Iterator raw = contourImage.upperLeft() + Diff2D(1,1);

    for(int y=-1; y<=height_; ++y, ++raw.y)
    {
        BImage::Iterator rx = raw;
        for(int x=-1; x<=width_; ++x, ++rx.x)
        {
            if(*rx == 0)
            {
                cells(x,y).setType(CellTypeRegion);
            }
            else
            {
                vigra::NeighborhoodCirculator<BImage::Iterator, EightNeighborCode>
                    neighbors(rx, EightNeighborCode::SouthEast);
                vigra::NeighborhoodCirculator<BImage::Iterator, EightNeighborCode>
                    end = neighbors;

                int conf = 0;
                do
                {
                    conf = (conf << 1) | *neighbors;
                }
                while(--neighbors != end);

                if(cellConfigurations[conf] == CellTypeError)
                {
                    char message[200];
                    sprintf(message, "FourEightSegmentation::init(): "
                            "Configuration at (%d, %d) must be thinned further",
                            x, y);

                    vigra_precondition(0, message);
                }

                cells(x,y).setType(cellConfigurations[conf]);
            }
        }
    }
}

// -------------------------------------------------------------------

int FourEightSegmentation::label0Cells()
{
    BImage nodeImage(width_+4, height_+4);
    BImage::Iterator nodes = nodeImage.upperLeft() + Diff2D(2,2);

    for(int y=-2; y<height_+2; ++y)
    {
        CellImage::Iterator cell = cells + Diff2D(-2, y);

        for(int x=-2; x<width_+2; ++x, ++cell.x)
        {
            if(cell->type() == CellTypeVertex)
            {
                nodes(x,y) = 1;

                // test for forbidden configuration
                CellImageEightCirculator n(cell);
                CellImageEightCirculator nend = n;

                do
                {
                    if(n->type() == CellTypeLine && n[1].type() == CellTypeLine)
                    {
                        char msg[200];
                        sprintf(msg, "initFourEightSegmentation(): "
                                "Node at (%d, %d) has two incident edgels from the same edge (direction: %d)",
                                x, y, n - nend);
                        vigra_precondition(0, msg);
                    }
                }
                while(++n != nend);
            }
            else
            {
                nodes(x,y) = 0;
            }
        }
    }

    return labelImageWithBackground(
        srcImageRange(nodeImage),
        destImage(cellImage, CellImageLabelWriter<CellTypeVertex>()), true, 0);
}

// -------------------------------------------------------------------

int FourEightSegmentation::label1Cells(int maxNodeLabel)
{
    std::vector<bool> nodeProcessed(maxNodeLabel + 1, false);

    int maxEdgeLabel = 0;

    for(int y=-1; y<=height_; ++y)
    {
        CellImage::Iterator cell = cells + Diff2D(-1, y);

        for(int x=-1; x<=width_; ++x, ++cell.x)
        {
            if(cell->type() != CellTypeVertex)
                continue;
            if(nodeProcessed[cell->label()])
                continue;

            nodeProcessed[cell->label()] = true;

            RayCirculator rayAtStart(
                this, CellImageEightCirculator(cell, EightNeighborCode::West));
            RayCirculator rayEnd = rayAtStart;

            do
            {
                if(rayAtStart.edgeLabel() != 0)
                    continue;

                labelEdge(rayAtStart.neighborCirculator(), ++maxEdgeLabel);
            }
            while(++rayAtStart != rayEnd);
        }
    }

    return maxEdgeLabel;
}

// -------------------------------------------------------------------

int FourEightSegmentation::label2Cells(BImage & contourImage)
{
    // labelImageWithBackground() starts with label 1, so don't
    // include outer border (infinite regions shall have label 0)
    return labelImageWithBackground(
        srcIterRange(contourImage.upperLeft() + Diff2D(1,1),
                     contourImage.lowerRight() - Diff2D(1,1),
                     contourImage.accessor()),
        destIter(cellImage.upperLeft() + Diff2D(1,1),
                 CellImageLabelWriter<CellTypeRegion>()),
        false, 1);
}

// -------------------------------------------------------------------

void FourEightSegmentation::labelCircles(int & maxNodeLabel, int & maxEdgeLabel)
{
    for(int y=-1; y<=height_; y++)
    {
        CellImage::Iterator cell = cells + Diff2D(-1, y);

        for(int x=-1; x<=width_; x++, cell.x++)
        {
            if(cell->label() != 0)
                continue;

            // found a circle (not labeled by previous steps)

            // mark first point as node
            (*cell) = CellPixel(CellTypeVertex, ++maxNodeLabel);

            CellImageEightCirculator rayAtStart(cell);
            CellImageEightCirculator rayEnd = rayAtStart;

            do
            {
                if(rayAtStart->type() != CellTypeLine)
                    continue;
                if(rayAtStart->label() != 0)
                    continue;

                labelEdge(rayAtStart, ++maxEdgeLabel);
            }
            while(++rayAtStart != rayEnd);
        }
    }
}

// -------------------------------------------------------------------

void FourEightSegmentation::labelEdge(CellImageEightCirculator rayAtStart,
                                      int newLabel)
{
    EdgelIterator edge(rayAtStart);

    // follow the edge and relabel it
    for(; !edge.isEnd(); ++edge)
    {
        edge->setLabel(newLabel, CellTypeLine);
    }
}

// -------------------------------------------------------------------

void FourEightSegmentation::initNodeList(int maxNodeLabel)
{
    nodeList.resize(maxNodeLabel + 1);
    std::vector<int> crackCirculatedAreas(maxNodeLabel + 1, 0);

    for(int y=-1; y<=height_; ++y)
    {
        CellImage::Iterator cell = cells + Diff2D(-1, y);

        for(int x=-1; x<=width_; ++x, ++cell.x)
        {
            if(cell->type() != CellTypeVertex)
                continue;

            int index = cell->label();
            vigra_precondition(index < nodeList.size(),
                               "nodeList must be large enough!");

            if(!nodeList[index].initialized())
            {
                nodeList[index].label = index;
                ++nodeCount_;

                nodeList[index].centerX = x;
                nodeList[index].centerY = y;
                nodeList[index].size = 1;
                nodeList[index].ray = RayCirculator(
                    this, CellImageEightCirculator(cell,
                                                   EightNeighborCode::West));

                // calculate degree of the node
                RayCirculator r = nodeList[index].ray;
                RayCirculator rend = nodeList[index].ray;
                nodeList[index].degree = 0;
                do
                {
                    ++nodeList[index].degree;
                }
                while(++r != rend);

                // calculate area from following the outer contour of the node
                CrackContourCirculator<CellImage::traverser> crack(cell);
                CrackContourCirculator<CellImage::traverser> crackend(crack);
                do
                {
                    crackCirculatedAreas[index] += crack.diff().x * crack.pos().y -
                                                   crack.diff().y * crack.pos().x;
                }
                while(++crack != crackend);

                crackCirculatedAreas[index] /= 2;
            }
            else
            {
                nodeList[index].centerX += x;
                nodeList[index].centerY += y;

                // calculate area from counting the pixels of the node
                nodeList[index].size += 1;
            }
        }
    }

    int i;
    for(i=0; i < nodeList.size(); ++i)
    {
        if(!nodeList[i].initialized())
            continue;

        nodeList[i].centerX /= nodeList[i].size;
        nodeList[i].centerY /= nodeList[i].size;

        // methods to calculate the area must yield identical values
        if(crackCirculatedAreas[i] != nodeList[i].size)
        {
            char msg[200];
            sprintf(msg, "FourEightSegmentation::initNodeList(): "
                    "Node %d at (%d, %d) has a hole", i,
                    nodeList[i].ray.center().x, nodeList[i].ray.center().y);
            vigra_precondition(0, msg);
        }
    }
}

// -------------------------------------------------------------------

void FourEightSegmentation::initEdgeList(int maxEdgeLabel)
{
    edgeList.resize(maxEdgeLabel + 1);

    NodeAccessor node;
    EdgeAccessor edge;

    NodeIterator n = nodeList.begin();
    NodeIterator nend = nodeList.end();

    for(; n != nend; ++n)
    {
        if(!n->initialized())
            continue;
        RayCirculator r = node.rayCirculator(n);
        RayCirculator rend = r;

        do
        {
            int index = edge.label(r);
            vigra_precondition(index < edgeList.size(),
                               "edgeList must be large enough!");
            if(!edgeList[index].initialized())
            {
                edgeList[index].label = index;
                ++edgeCount_;
                edgeList[index].start = r;
                edgeList[index].end = r;
                edgeList[index].end.jumpToOpposite();
            }
        }
        while(++r != rend);
    }
}

// -------------------------------------------------------------------

void FourEightSegmentation::initFaceList(BImage & contourImage, int maxFaceLabel)
{
    faceList.resize(maxFaceLabel + 1);

    IImage contourLabelImage(width_ + 4, height_ + 4);
    contourLabelImage = 0;
    int contourComponentsCount =
        labelImageWithBackground(srcImageRange(contourImage),
                                 destImage(contourLabelImage), true, 0);
    IImage::Iterator contourLabel =
        contourLabelImage.upperLeft() + Diff2D(2, 2);

    std::vector<bool> contourProcessed(contourComponentsCount + 1, false);

    // process outer face
    faceList[0].label= 0;
    ++faceCount_;
    faceList[0].anchor = Diff2D(-2, -2);
    RayCirculator ray(this, CellImageEightCirculator(cells + Diff2D(-1, -1),
                                                     EightNeighborCode::West));
    --ray;
    faceList[0].contours.push_back(ContourCirculator(ray));
    contourProcessed[contourLabel(-1, -1)] = true;

    FaceAtLeftAccessor leftFace;

    for(int y=0; y<height_; ++y)
    {
        CellImage::Iterator cell = cells + Diff2D(0, y);
        CellImage::Iterator leftNeighbor = cells + Diff2D(-1, y);

        for(int x=0; x<width_; ++x, ++cell.x, ++leftNeighbor.x)
        {
            if(cell->type() != CellTypeRegion)
                continue;

            int index = cell->label();
            vigra_precondition(index < faceList.size(),
                               "faceList must be large enough!");

            if(!faceList[index].initialized())
            {
                faceList[index].label = index;
                ++faceCount_;
                faceList[index].anchor = Diff2D(x,y);

                // find incident node
                if(leftNeighbor->type() == CellTypeVertex)
                {
                    vigra_precondition(leftNeighbor->type() == CellTypeVertex,
                                       "leftNeighbor expected to be a vertex");

                    RayCirculator ray(
                        this, CellImageEightCirculator(leftNeighbor));
                    --ray;

                    vigra_invariant(leftFace.label(ray) == index,
                                    "FourEightSegmentation::initFaceList()");

                    faceList[index].contours.push_back(ContourCirculator(ray));
                }
                else
                {
                    vigra_precondition(leftNeighbor->type() == CellTypeLine,
                                       "leftNeighbor should be an edge");

                    int edgeIndex = leftNeighbor->label();

                    vigra_precondition(edgeList[edgeIndex].initialized(),
                                       "EdgeInfo expected to be initialized");

                    ContourCirculator c(edgeList[edgeIndex].start);
                    if(leftFace.label(c) != index)
                        c.jumpToOpposite();

                    vigra_invariant(leftFace.label(c) == index,
                                    "FourEightSegmentation::initFaceList()");

                    faceList[index].contours.push_back(c);
                }
            }
            else
            {
                // look for inner contours
                CellImageEightCirculator neighbor(cell);
                CellImageEightCirculator nend = neighbor;

                do
                {
                    int boundaryIndex = contourLabel[neighbor.base() - cells];
                    if(boundaryIndex == 0 || contourProcessed[boundaryIndex])
                        continue;

                    // found an inner contour
                    contourProcessed[boundaryIndex] = true;

                    // find incident node
                    if(neighbor->type() == CellTypeVertex)
                    {
                        // this is the node
                        CellImageEightCirculator n = neighbor;
                        n.swapCenterNeighbor();
                        RayCirculator ray(this, n);
                        --ray;

                        vigra_invariant(leftFace.label(ray) == index,
                                        "FourEightSegmentation::initFaceList()");

                        faceList[index].contours.push_back(ContourCirculator(ray));
                    }
                    else
                    {
                        vigra_precondition(neighbor->type() == CellTypeLine,
                                           "neighbor expected to be an edge");

                        int edgeIndex = neighbor->label();

                        vigra_precondition(edgeList[edgeIndex].initialized(),
                                           "EdgeInfo should be initialized");

                        ContourCirculator c(edgeList[edgeIndex].start);
                        if(leftFace.label(c) != index)
                            c.jumpToOpposite();

                        vigra_invariant(leftFace.label(c) == index,
                                        "FourEightSegmentation::initFaceList()");

                        faceList[index].contours.push_back(c);
                    }
                }
                while(++neighbor != nend);
            }
        }
    }
}

struct CellIndexAccessor
{
    typedef int value_type;

    int maxNodeLabel_, maxEdgeLabel_;

    CellIndexAccessor(int maxNodeLabel, int maxEdgeLabel)
        : maxNodeLabel_(maxNodeLabel), maxEdgeLabel_(maxEdgeLabel)
    {
    }

    template<class Iterator>
    int operator()(const Iterator &it) const
    {
        return it->label()
            + (it->type() == CellTypeVertex ? 0 : maxNodeLabel_ + 1)
            + (it->type() != CellTypeRegion ? 0 : maxEdgeLabel_ + 1);
    }
};

void FourEightSegmentation::initBoundingBoxes(int maxNodeLabel, int maxEdgeLabel,
                                              int maxFaceLabel)
{
    ArrayOfRegionStatistics<FindBoundingRectangle>
        bounds(maxNodeLabel + maxEdgeLabel + maxFaceLabel + 3);

    inspectTwoImages(srcIterRange(Diff2D(-2, -2), cellImage.size() - Diff2D(2, 2)),
                     srcImage(cellImage,
                              CellIndexAccessor(maxNodeLabel, maxEdgeLabel)),
                     bounds);

    // copy all bounding rects into the CellInfo structs, ignoring that
    // possibly !cellList[cell].initialized() resp. !bounds[cell].valid
    for(int node= 0; node<= maxNodeLabel; ++node)
    {
        nodeList[node].upperLeft = bounds[node].upperLeft;
        nodeList[node].lowerRight = bounds[node].lowerRight;
    }
    int edge0 = maxNodeLabel + 1;
    for(int edge= 0; edge<= maxEdgeLabel; ++edge)
    {
        edgeList[edge].upperLeft = bounds[edge + edge0].upperLeft;
        edgeList[edge].lowerRight = bounds[edge + edge0].lowerRight;
    }
    int face0 = maxNodeLabel + maxEdgeLabel + 2;
    for(int face= 0; face<= maxFaceLabel; ++face)
    {
        faceList[face].upperLeft = bounds[face + face0].upperLeft;
        faceList[face].lowerRight = bounds[face + face0].lowerRight;
    }
}

} // namespace CellImage

} // namespace vigra

#endif /* VIGRA_FOUREIGHTSEGMENTATION_HXX */
