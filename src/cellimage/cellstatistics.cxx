#include <foureightsegmentation.hxx>
#include "cellstatistics.hxx"
#include "mydebug.hxx"
#include "debugimage.hxx"
#include "crop.hxx"

#include <vigra/stdimage.hxx>
#include <vigra/seededregiongrowing.hxx>
#include <vigra/transformimage.hxx>
#include <vigra/flatmorphology.hxx>
#include <vigra/pixelneighborhood.hxx>

#include <functional>
#include <algorithm>
#include <numeric>

namespace std {

void swap(SegmentationData &a, SegmentationData &b)
{
    swap(a.preparedOriginal_,  b.preparedOriginal_);
    swap(a.gradientMagnitude_, b.gradientMagnitude_);
    swap(a.edgeDirection_,     b.edgeDirection_);
    swap(a.edgeDirGradient_,   b.edgeDirGradient_);
}

} // namespace std

template<class RegionStatistics, class LabelType = int>
class ArrayOfIdenticalStatistics
{
public:
        /** label type is also used to determine the region to be
         * returned by the 1 argument operator()
         */
    typedef LabelType argument_type;

        /** result type of the contained statistics object becomes
         * result type of the analyser
         */
    typedef typename RegionStatistics::result_type result_type;

        /** the value type of the array: the contained statistics
         * object.
         */
    typedef RegionStatistics value_type;

        /** the array's reference type
         */
    typedef RegionStatistics & reference;

        /** the array's const reference type
         */
    typedef RegionStatistics const & const_reference;

        /** init array of RegionStatistics
         */
    ArrayOfIdenticalStatistics()
    {}

        /** reset the contained functors to their initial state.
         */
    void reset()
    {
        stats_ = RegionStatistics();
    }

        /** access the statistics for a region via its label. (always
         * returns the same statistics object)
         */
    result_type operator()(argument_type /*label*/) const
        { return stats_; }

        /** read the statistics functor for a region via its label
         * (always returns the same statistics object)
         */
    const_reference operator[](argument_type /*label*/) const
        { return stats_; }

        /** access the statistics functor for a region via its label
         * (always returns the same statistics object)
         */
    reference operator[](argument_type /*label*/)
        { return stats_; }

private:
    RegionStatistics stats_;
};

CellStatistics::CellStatistics(const Segmentation &initialSegmentation,
                               SegmentationData  *segmentationData)
: segmentationData(segmentationData),
  segDataBounds(initialSegmentation.cellImage.size()), // border added below
  lastChanges_(vigra::Point2D(-2, -2), initialSegmentation.cellImage.size())
{
    segDataBounds.addBorder(
        (segmentationData->preparedOriginal_.width() -
         initialSegmentation.cellImage.width()) / 2,
        (segmentationData->preparedOriginal_.height() -
         initialSegmentation.cellImage.height()) / 2);

    std::cerr << "initializing face statistics (max face label: "
              << initialSegmentation.maxFaceLabel() << ")\n";

    faceStatistics_.resize(initialSegmentation.maxFaceLabel() + 1);

    Segmentation::ConstFaceIterator faceIt = initialSegmentation.facesBegin();
    for(++faceIt; faceIt.inRange(); ++faceIt)
    {
        inspectCell(initialSegmentation.faceScanIterator
                    (faceIt->label, segmentationData->preparedOriginal_.upperLeft()),
                    faceStatistics_[faceIt->label]);
    }

    std::cerr << "finding node centers.. (max node label: "
              << initialSegmentation.maxNodeLabel() << ")\n";
    nodeCenters_.resize(initialSegmentation.maxNodeLabel() + 1);
    Segmentation::ConstNodeIterator nodeIt = initialSegmentation.nodesBegin();
    for(; nodeIt.inRange(); ++nodeIt)
    {
        //std::cerr << "finding center of node " << nodeIt->label << "..\n";
        vigra::cellimage::LabelScanIterator<
            vigra::cellimage::CellImage::traverser, vigra::Point2D>
            nodeScanner(
                initialSegmentation.nodeScanIterator(
                    nodeIt->label, vigra::Point2D(0, 0), false));

        for(; nodeScanner.inRange(); ++nodeScanner)
        {
            //std::cerr << "  has pixel at " << *nodeScanner << std::endl;
            nodeCenters_[nodeIt->label][0] += nodeScanner->x;
            nodeCenters_[nodeIt->label][1] += nodeScanner->y;
        }
        nodeCenters_[nodeIt->label] /= nodeIt->size;
        //std::cerr << "  center: " << nodeCenters_[nodeIt->label] << "..\n";
    }

    std::cerr << "initializing configurationDirections\n";
    configurationDirections_.resize(256);
    for(unsigned char i= 1; i<255; ++i)
    {
        vigra::EightNeighborOffsetCirculator circ;
        unsigned char code = i;

        //std::cerr << "  " << (int)i;

        // combine bits at start and end of code to one block:
        while((code&1) && (code&128))
        {
            ++circ;
            code = (code >> 1) | 128;
        }

        // collect(==sum up) diff()s to first block of bits:
        vigra::Diff2D diff1(0, 0);
        for(; (code&1) == 0; code >>= 1, ++circ)
            ;
        for(; (code&1) != 0; code >>= 1, ++circ)
            diff1 += *circ;

         // no second block of bits? no edge configuration -> skip
        if(!code)
        {
            //std::cerr << " is vertex config: only one block of bits\n";
            continue;
        }

        // collect(==sum up) diff()s to second block of bits:
        vigra::Diff2D diff2(0, 0);
        for(; (code&1) == 0; code >>= 1, ++circ)
            ;
        for(; (code&1) != 0; code >>= 1, ++circ)
            diff2 += *circ;

         // third block of bits? no edge configuration -> skip
        if(code)
        {
            //std::cerr << " is vertex config: > 2 blocks of bits\n";
            continue;
        }

        configurationDirections_[i] =
            Float2D(diff2.x - diff1.x, diff2.y - diff1.y);
        configurationDirections_[i] /= configurationDirections_[i].magnitude();
        /*std::cerr << " has direction "
                  << configurationDirections_[i][0] << " / "
                  << configurationDirections_[i][1] << "\n";*/
    }

    std::cerr << "initializing meanEdgeGradients\n";

    edgeStatistics_.resize(initialSegmentation.maxEdgeLabel() + 1);

    for(Segmentation::ConstEdgeIterator it = initialSegmentation.edgesBegin();
        it.inRange(); ++it)
    {
        const Segmentation::DartTraverser &anchor = it->start;
        if(!anchor.leftFaceLabel() || !anchor.rightFaceLabel())
            edgeStatistics_[it->label](
                vigra::NumericTraits<GradientImage::PixelType>::max());
        else
        {
            inspectCell(initialSegmentation.edgeScanIterator
                        (it->label, segmentationData->gradientMagnitude_.upperLeft()),
                        edgeStatistics_[it->label]);
        }
    }

    std::cerr << "initializing tree of merged edges\n";
    mergedEdges_.resize(initialSegmentation.maxEdgeLabel() + 1);
    //std::iota(mergedEdges_.begin(), mergedEdges_.end(), 0);
    for(unsigned int i = 0; i < mergedEdges_.size(); ++i)
        mergedEdges_[i] = i;
}

struct FetchRegionsFunctor
: public std::unary_function<vigra::cellimage::CellPixel, int>
{
    vigra::cellimage::CellPixel removePixel_;

    FetchRegionsFunctor(vigra::cellimage::CellPixel removePixel)
    : removePixel_(removePixel)
    {}

    int operator()(const vigra::cellimage::CellPixel &p) const
    {
        if(p == removePixel_)
            return 0;
        // make sure other nodes and edges will survive:
        if(p.type() != vigra::cellimage::CellTypeRegion)
            return vigra::SRGWatershedLabel;
        return p.label();
    }
};

void nodeRethinning(vigra::cellimage::FourEightSegmentation &seg,
                    const GradientImage &/*gradientMagnitude*/,
                    vigra::cellimage::CellLabel nodeLabel)
{
    typedef vigra::cellimage::FourEightSegmentation
        FourEightSegmentation;

    FourEightSegmentation::NodeInfo &node(seg.node(nodeLabel));

    if(node.size < 2)
        return;

    vigra::cellimage::CellPixel
        nodePixel(vigra::cellimage::CellTypeVertex, nodeLabel);

    for(FourEightSegmentation::CellScanIterator nodeScanner =
            seg.nodeScanIterator(nodeLabel, seg.cells, false);
        nodeScanner.inRange(); ++nodeScanner)
    {
        vigra::cellimage::CellImageEightCirculator
            circ(nodeScanner, vigra::EightNeighborCode::North);
        do
        {
            if(circ->type() == vigra::cellimage::CellTypeRegion)
            {
                vigra::cellimage::CellImageEightCirculator circ2(circ);
                do
                {
                    if((*circ2 != *circ) && (*circ2 != nodePixel))
                    {
                        if(!(circ2.isDiagonal() &&
                             (circ2->type() == vigra::cellimage::CellTypeLine &&
                              (circ2[1].type() == vigra::cellimage::CellTypeVertex ||
                               circ2[-1].type() == vigra::cellimage::CellTypeVertex))))
                            goto ContinueWithNextNodePixel;
                    }
                }
                while((++circ2).direction() != circ.direction());
                // only one neighbored region found -> remove node pixel
                *nodeScanner = *circ; // relabel pixel
                --node.size;
                ++seg.face(circ->label()).size;
                // FIXME: update node.bounds?
                seg.face(circ->label()).bounds |=
                    vigra::Point2D((vigra::cellimage::CellImage::traverser)nodeScanner - seg.cells);
                if(node.size < 2)
                    return;
                goto ContinueWithNextNodePixel;
            }
        }
        while((++circ).direction() != vigra::EightNeighborCode::North);
    ContinueWithNextNodePixel:
        ;
    }
}

struct ClearROIFunctor
{
    int operator()(int label, vigra::BImage::PixelType maskValue) const
    {
        if(maskValue)
            return 0;
        else
            return label;
    }
};

void edgeRethinning(vigra::cellimage::FourEightSegmentation &seg,
                    const GradientImage &gradientMagnitude,
                    vigra::cellimage::CellLabel edgeLabel,
                    const vigra::Rect2D &rethinRange)
{
    //std::cerr << "edgeRethinning(edgeLabel: " << edgeLabel << ")\n";

    typedef vigra::cellimage::FourEightSegmentation
        FourEightSegmentation;

    FourEightSegmentation::EdgeInfo &edge(seg.edge(edgeLabel));

    vigra::cellimage::CellLabel face1Label(edge.start.leftFaceLabel());
    vigra::cellimage::CellLabel face2Label(edge.start.rightFaceLabel());
    if(face1Label == face2Label)
        return; // watershed can not work on bridges :-(

    FourEightSegmentation::FaceInfo &face1(seg.face(face1Label));
    FourEightSegmentation::FaceInfo &face2(seg.face(face2Label));

    vigra::cellimage::CellPixel
        edgePixel(vigra::cellimage::CellTypeLine, edgeLabel);
    vigra::cellimage::CellPixel
        face1Pixel(vigra::cellimage::CellTypeRegion, face1Label);
    vigra::cellimage::CellPixel
        face2Pixel(vigra::cellimage::CellTypeRegion, face2Label);

    // now fetch boundaries
    //std::cerr << "  performing watershed in " << rethinRange.size() << "-region\n";
    vigra::IImage newRegions(rethinRange.size());
    transformImage(crop(srcIterRange(seg.cells, seg.cells), rethinRange),
                   destImage(newRegions),
                   FetchRegionsFunctor(edgePixel));

    // perform region growing again for thinning
    ArrayOfIdenticalStatistics<vigra::SeedRgDirectValueFunctor<GradientImage::PixelType> >
        stats;
    seededRegionGrowing(crop(srcImageRange(gradientMagnitude), rethinRange),
                        srcImage(newRegions),
                        destImage(newRegions), stats, vigra::KeepContours);

    // label new CellTypeRegion pixels from result of seededRegionGrowing
    typedef vigra::cellimage::CellImage::traverser
        CellTraverser;

    // collect new edge properties on the fly
    edge.bounds = vigra::Rect2D();

    vigra::Point2D row = rethinRange.upperLeft();
    CellTraverser cellRow = seg.cells + rethinRange.upperLeft();
    vigra::IImage::traverser regionsRow = newRegions.upperLeft();
    CellTraverser lr = seg.cells + rethinRange.lowerRight();
    for(; cellRow.y < lr.y; ++row.y, ++cellRow.y, ++regionsRow.y)
    {
        vigra::Point2D pos = row;
        CellTraverser cell = cellRow;
        vigra::IImage::traverser newLabel = regionsRow;
        for(; cell.x < lr.x; ++pos.x, ++cell.x, ++newLabel.x)
        {
            if(*cell == edgePixel)
            {
                if(*newLabel == 0)
                {
                    edge.bounds |= pos;
                    continue;
                }

                --edge.size;
                if(*newLabel == (int)face1Label)
                {
                    face1.bounds |= pos;
                    ++face1.size;
                    *cell = face1Pixel;
                }
                else if(*newLabel == (int)face2Label)
                {
                    face2.bounds |= pos;
                    ++face2.size;
                    *cell = face2Pixel;
                }
            }
        }
    }

#ifndef NDEBUG
    vigra_postcondition(edge.start.edgeLabel() == edgeLabel,
                        "edge ends relocated by edgeRethinning()");
    vigra_postcondition(edge.end.edgeLabel() == edgeLabel,
                        "edge ends relocated by edgeRethinning()");
#endif // NDEBUG
}
