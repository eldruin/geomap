#ifndef CELLPYRAMID_HXX
#define CELLPYRAMID_HXX

#include <vector>
#include <map>

/* Known design weaknesses:
 * - performOperation() does not increase index_ (because I liked to
 *   return...; directly for efficiency, but cannot ++index_ before
 *   that because of exception safety) :-(
 * - cutHead() calls storeCheckpoint to restore nextCheckpointLevelIndex_
 */

namespace vigra {

template<class SEGMENTATION, class CELLSTATISTICS>
class CellPyramid
{
  public:
    typedef SEGMENTATION Segmentation;
    typedef CELLSTATISTICS CellStatistics;

    typedef typename Segmentation::CellInfo CellInfo;
    typedef typename Segmentation::NodeInfo NodeInfo;
    typedef typename Segmentation::EdgeInfo EdgeInfo;
    typedef typename Segmentation::FaceInfo FaceInfo;
    typedef typename Segmentation::DartTraverser DartTraverser;

  protected:
    friend class CellPyramid<Segmentation, CellStatistics>::Level;

    enum OperationType { RemoveIsolatedNode,
                         MergeFaces,
                         RemoveBridge,
                         MergeEdges,
                         // composed Operations:
                         RemoveEdge,
                         RemoveEdgeWithEnds };

    struct Operation
    {
        OperationType                      type;
        typename DartTraverser::Serialized param;

        Operation(OperationType opType, DartTraverser paramDart)
        : type(opType), param(paramDart.serialize()) {}
    };

  public:
    class Level
    {
      public:
        typedef CellPyramid<Segmentation, CellStatistics> Pyramid;
        friend class Pyramid;

        const unsigned int index() const
        { return index_; }
        const Segmentation &segmentation() const
        { return segmentation_; }
        const CellStatistics &cellStatistics() const
        { return cellStatistics_; }

        Level(unsigned int l,
              const Segmentation &s,
              const CellStatistics &c,
              Pyramid *p)
        : index_(l), segmentation_(s), cellStatistics_(c), pyramid_(p)
        {}

            /** Do a maximum of maxSteps operations to reach given level.
             * Returns true if that was enough, that is (topLevel().index() ==
             * levelIndex)
             */
        bool approachLevel(unsigned int gotoLevelIndex, unsigned int maxSteps = 20)
        {
            unsigned int step =
                gotoLastCheckpointBefore(gotoLevelIndex) ? 1 : 0;
            
            while((index_ < gotoLevelIndex) && (step++ < maxSteps))
            {
                performOperation(pyramid_->history_[index_]);
                ++index_;
            }
            
            return (index_ == gotoLevelIndex);
        }

        void gotoLevel(unsigned int gotoLevelIndex)
        {
            gotoLastCheckpointBefore(gotoLevelIndex);

            while(index_ < gotoLevelIndex)
            {
                performOperation(pyramid_->history_[index_]);
                ++index_;
            }
        }

        void cutHead()
        {
            pyramid_->cutAbove(*this);
        }

      protected:
            /** Returns false (and does not change this level) if the given
             * levelData is a "better" position for reaching levelIndex
             * than the last checkpoint, that is:
             *
             * lastCheckpointIt->first < levelData->LevelIndex() < levelIndex
             */
        bool gotoLastCheckpointBefore(unsigned int levelIndex)
        {
            typename Pyramid::CheckpointMap::iterator lastCheckpointIt =
                pyramid_->checkpoints_.upper_bound(levelIndex);
            --lastCheckpointIt;

            if((index() <= levelIndex) && (lastCheckpointIt->first <= index()))
                return false;

            std::cerr << "to get from level " << index() << " to " << levelIndex
                      << ", we use checkpoint " << lastCheckpointIt->first << "\n";

            operator=(lastCheckpointIt->second);
            return true;
        }

        FaceInfo &removeIsolatedNodeInternal(const DartTraverser & dart)
        {
            cellStatistics_.preRemoveIsolatedNode(dart);
            FaceInfo &result(segmentation_.removeIsolatedNode(dart));
            cellStatistics_.postRemoveIsolatedNode(result);
            return result;
        }

        FaceInfo &mergeFacesInternal(const DartTraverser & dart)
        {
            cellStatistics_.preMergeFaces(dart);
            FaceInfo &result(segmentation_.mergeFaces(dart));
            cellStatistics_.postMergeFaces(result);
            return result;
        }

        FaceInfo &removeBridgeInternal(const DartTraverser & dart)
        {
            cellStatistics_.preRemoveBridge(dart);
            FaceInfo &result(segmentation_.removeBridge(dart));
            cellStatistics_.postRemoveBridge(result);
            return result;
        }

        EdgeInfo &mergeEdgesInternal(const DartTraverser & dart)
        {
            cellStatistics_.preMergeEdges(dart);
            EdgeInfo &result(segmentation_.mergeEdges(dart));
            cellStatistics_.postMergeEdges(result);
            return result;
        }

        CellInfo &performOperation(Operation &op)
        {
            DartTraverser param(&segmentation_, op.param);
            
            switch(op.type)
            {
              case RemoveIsolatedNode:
              {
                  return removeIsolatedNodeInternal(param);
              }
              case MergeFaces:
              {
                  return mergeFacesInternal(param);
              }
              case RemoveBridge:
              {
                  return removeBridgeInternal(param);
              }
              case MergeEdges:
              {
                  return mergeEdgesInternal(param);
              }
              case RemoveEdge:
              {
                  return (param.leftFaceLabel() == param.rightFaceLabel() ?
                          removeBridgeInternal(param) :
                          mergeFacesInternal(param));
              }
              case RemoveEdgeWithEnds:
              {
                  EdgeInfo &removedEdge = segmentation_.edge(param.edgeLabel());
                  NodeInfo &node1(removedEdge.start.startNode());
                  NodeInfo &node2(removedEdge.end.startNode());

                  FaceInfo &result = (param.leftFaceLabel() == param.rightFaceLabel() ?
                                      removeBridgeInternal(param) :
                                      mergeFacesInternal(param));

                  if(node1.degree == 0)
                      removeIsolatedNodeInternal(node1.anchor);

                  if((node1.label != node2.label) && (node2.degree == 0))
                      removeIsolatedNodeInternal(node2.anchor);

                  return result;
              }
            }

            vigra_fail("Unknown operation type in CellPyramid<>::performOperation!");
            return segmentation_.face(0);
        }

        unsigned int   index_;
        Segmentation   segmentation_;
        CellStatistics cellStatistics_;
        Pyramid       *pyramid_;
    };

  private:
    typedef std::map<unsigned int, Level>
        CheckpointMap;
    CheckpointMap checkpoints_;

    typedef std::vector<Operation>
        History;
    History history_;

    Level topLevel_;
    unsigned int nextCheckpointLevelIndex_;

    CellInfo &addAndPerformOperation(OperationType t, const DartTraverser &p)
    {
        vigra_precondition(topLevel_.index() == levelCount()-1,
            "addAndPerformOperation(): topLevel_ is not the top level anymore");

        CellInfo *result = NULL;
        try
        {
            history_.push_back(Operation(t, p));
            result = &topLevel_.performOperation(history_.back());
            ++topLevel_.index_;
            if(topLevel_.index() == nextCheckpointLevelIndex_)
                storeCheckpoint(topLevel_);
        }
        catch(...)
        {
            cutAbove(topLevel_.index());
            throw;
        }
        return *result;
    }

        // called after adjusting topLevel_ to remove levels above
    void cutHead()
    {
        history_.erase(history_.begin() + topLevel_.index(), history_.end());
        checkpoints_.erase(checkpoints_.upper_bound(topLevel_.index()),
                           checkpoints_.end());
        typename CheckpointMap::iterator lastCheckpointIt = checkpoints_.end();
        storeCheckpoint((--lastCheckpointIt)->second);
    }

  public:
    void storeCheckpoint(const Level &level)
    {
        if(!checkpoints_.count(level.index()))
            checkpoints_.insert(std::make_pair(topLevel().index(), level));

        unsigned int totalCellCount =
            level.segmentation().nodeCount() +
            level.segmentation().edgeCount() +
            level.segmentation().faceCount();
        if(totalCellCount > 30)
            nextCheckpointLevelIndex_ = level.index() + totalCellCount / 4;
        else
            nextCheckpointLevelIndex_ = level.index() + 10;

        std::cerr << "--- stored checkpoint at level #" << level.index()
                  << ", " << totalCellCount << " cells total left ---\n";
    }

    CellPyramid(const Segmentation &level0,
                const CellStatistics &level0Stats = CellStatistics())
    : topLevel_(0, level0, level0Stats, this),
      nextCheckpointLevelIndex_(0)
    {
        storeCheckpoint(topLevel_);
    }

    FaceInfo &removeIsolatedNode(const DartTraverser & dart)
    {
        return static_cast<FaceInfo &>(
            addAndPerformOperation(RemoveIsolatedNode, dart));
    }

    FaceInfo &mergeFaces(const DartTraverser & dart)
    {
        return static_cast<FaceInfo &>(
            addAndPerformOperation(MergeFaces, dart));
    }

    FaceInfo &removeBridge(const DartTraverser & dart)
    {
        return static_cast<FaceInfo &>(
            addAndPerformOperation(RemoveBridge, dart));
    }

    EdgeInfo &mergeEdges(const DartTraverser & dart)
    {
        return static_cast<EdgeInfo &>(
            addAndPerformOperation(MergeEdges, dart));
    }

    FaceInfo &removeEdge(const DartTraverser & dart)
    {
        return static_cast<FaceInfo &>(
            addAndPerformOperation(RemoveEdge, dart));
    }

    FaceInfo &removeEdgeWithEnds(const DartTraverser & dart)
    {
        return static_cast<FaceInfo &>(
            addAndPerformOperation(RemoveEdgeWithEnds, dart));
    }

    Level &topLevel()
    {
        return topLevel_;
    }

    const Level &topLevel() const
    {
        return topLevel_;
    }

    Level *getLevel(unsigned int levelIndex)
    {
        typename CheckpointMap::iterator lastCheckpointIt =
            checkpoints_.upper_bound(levelIndex);
        --lastCheckpointIt;
        Level *result = new Level(lastCheckpointIt->second);
        result->gotoLevel(levelIndex);
        return result;
    }

    unsigned int levelCount() const
    {
        return history_.size() + 1;
    }

    void cutAbove(const Level &level)
    {
        vigra_precondition(topLevel_.index() == levelCount()-1,
                           "cutAbove(): topLevel_ is not the top level anymore");
        if(topLevel_.index() != level.index())
        {
            topLevel_ = level;
            cutHead();
        }
    }

    void cutAbove(unsigned int levelIndex)
    {
        vigra_precondition(topLevel_.index() == levelCount()-1,
                           "cutAbove(): topLevel_ is not the top level anymore");
        if(topLevel_.index() != levelIndex)
        {
            topLevel_.gotoLevel(levelIndex);
            cutHead();
        }
    }
};

} // namespace vigra

#endif // CELLPYRAMID_HXX
