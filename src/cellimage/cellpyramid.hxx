#ifndef CELLPYRAMID_HXX
#define CELLPYRAMID_HXX

#include <vector>
#include <map>

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

    struct Level
    {
        const unsigned int levelIndex() const
        { return levelIndex_; }
        const Segmentation &segmentation() const
        { return segmentation_; }
        const CellStatistics &cellStatistics() const
        { return cellStatistics_; }

        Level(unsigned int l,
              const Segmentation &s,
              const CellStatistics &c)
        : levelIndex_(l), segmentation_(s), cellStatistics_(c)
        {}

      protected:
        unsigned int   levelIndex_;
        Segmentation   segmentation_;
        CellStatistics cellStatistics_;

        friend class CellPyramid<Segmentation, CellStatistics>;
    };

    friend struct CellPyramid<Segmentation, CellStatistics>::Level;

  private:
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

    typedef std::map<unsigned int, Level>
        CheckpointMap;
    CheckpointMap checkpoints_;

    typedef std::vector<Operation>
        History;
    History history_;

    Level currentLevel_;
    unsigned int nextCheckpointLevelIndex_;

    FaceInfo &removeIsolatedNodeInternal(const DartTraverser & dart)
    {
        currentLevel_.cellStatistics_.preRemoveIsolatedNode(dart);
        FaceInfo &result(currentLevel_.segmentation_.removeIsolatedNode(dart));
        currentLevel_.cellStatistics_.postRemoveIsolatedNode(result);
        return result;
    }

    FaceInfo &mergeFacesInternal(const DartTraverser & dart)
    {
        currentLevel_.cellStatistics_.preMergeFaces(dart);
        FaceInfo &result(currentLevel_.segmentation_.mergeFaces(dart));
        currentLevel_.cellStatistics_.postMergeFaces(result);
        return result;
    }

    FaceInfo &removeBridgeInternal(const DartTraverser & dart)
    {
        currentLevel_.cellStatistics_.preRemoveBridge(dart);
        FaceInfo &result(currentLevel_.segmentation_.removeBridge(dart));
        currentLevel_.cellStatistics_.postRemoveBridge(result);
        return result;
    }

    EdgeInfo &mergeEdgesInternal(const DartTraverser & dart)
    {
        currentLevel_.cellStatistics_.preMergeEdges(dart);
        EdgeInfo &result(currentLevel_.segmentation_.mergeEdges(dart));
        currentLevel_.cellStatistics_.postMergeEdges(result);
        return result;
    }

    CellInfo &performOperation(Operation &op, Level *levelData)
    {
        DartTraverser param(&levelData->segmentation_, op.param);

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
              EdgeInfo &removedEdge =
                  levelData->segmentation_.edge(param.edgeLabel());
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
        return levelData->segmentation_.face(0);
    }

        /** Returns NULL (and does not change levelData) if the given
         * levelData is a "better" position for reaching levelIndex
         * than the last checkpoint, that is:
         *
         * lastCheckpointIt->first < levelData->LevelIndex() < levelIndex
         *
         * Else, returns levelData, or if levelData was NULL, the
         * pointer to a newly allocated Level with the last
         * checkpoint.
         */
    Level *gotoLastCheckpointBefore(unsigned int levelIndex,
                                    Level *levelData)
    {
        vigra_precondition(levelIndex < levelCount(),
                           "trying to access non-existing level (too high index)!");

        typename CheckpointMap::iterator lastCheckpointIt =
            checkpoints_.upper_bound(levelIndex);
        --lastCheckpointIt;

        if(!levelData)
            return new Level(lastCheckpointIt->second);

        if((levelData->levelIndex() < levelIndex) &&
           (lastCheckpointIt->first < levelData->levelIndex()))
            return NULL;

        *levelData = lastCheckpointIt->second;
        return levelData;
    }

    CellInfo &changeLevelIndexInternal(unsigned int gotoLevelIndex,
                                       Level *levelData)
    {
        gotoLastCheckpointBefore(gotoLevelIndex, levelData);

        CellInfo *result = NULL; // &levelData->segmentation_.face(0);

        while(levelData->levelIndex_ < gotoLevelIndex)
        {
            result = &performOperation(history_[levelData->levelIndex_], levelData);
            if(++levelData->levelIndex_ == nextCheckpointLevelIndex_)
                storeCheckpoint();
        }

        return *result;
    }

    CellInfo &addAndPerformOperation(OperationType t, const DartTraverser &p)
    {
        try
        {
            history_.push_back(Operation(t, p));
            return changeLevelIndexInternal(levelCount()-1, &currentLevel_);
        }
        catch(...)
        {
            cutHead();
            throw;
        }
    }

  public:
    void storeCheckpoint()
    {
        if(!checkpoints_.count(currentLevelIndex()))
            checkpoints_.insert(std::make_pair(currentLevelIndex(), currentLevel_));

        unsigned int totalCellCount =
            currentLevel_.segmentation_.nodeCount() +
            currentLevel_.segmentation_.edgeCount() +
            currentLevel_.segmentation_.faceCount();
        if(totalCellCount > 30)
            nextCheckpointLevelIndex_ = currentLevelIndex() + totalCellCount / 4;
        else
            nextCheckpointLevelIndex_ = currentLevelIndex() + 10;

        std::cerr << "--- stored checkpoint at level #" << currentLevelIndex()
                  << ", " << totalCellCount << " cells total left ---\n";
    }

    CellPyramid(const Segmentation &level0,
                const CellStatistics &level0Stats = CellStatistics())
    : currentLevel_(0, level0, level0Stats),
      nextCheckpointLevelIndex_(0)
    {
        storeCheckpoint();
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

    Level &currentLevel()
    {
        return currentLevel_;
    }

    const Level &currentLevel() const
    {
        return currentLevel_;
    }

    const unsigned int currentLevelIndex() const
    {
        return currentLevel_.levelIndex();
    }

    const Segmentation &currentSegmentation() const
    {
        return currentLevel_.segmentation();
    }

    const CellStatistics &currentCellStatistics() const
    {
        return currentLevel_.cellStatistics();
    }

    const Level &gotoLevel(unsigned int levelIndex)
    {
        changeLevelIndexInternal(levelIndex, &currentLevel_);
        return currentLevel_;
    }

    Level *getLevel(unsigned int levelIndex)
    {
        Level *result(gotoLastCheckpointBefore(levelIndex, NULL));
        changeLevelIndexInternal(levelIndex, result);
        return result;
    }

        /** Do a maximum of maxSteps operations to reach given level.
         * Returns true if that was enough, that is (currentLevelIndex() ==
         * levelIndex)
         */
    bool approachLevel(unsigned int levelIndex, unsigned int maxSteps = 20,
                       Level *levelData = NULL)
    {
        if(!levelData)
            levelData = &currentLevel_;

        unsigned int step =
            gotoLastCheckpointBefore(levelIndex, levelData) ? 1 : 0;

        while((step++ < maxSteps) && (levelData->levelIndex_ < levelIndex))
        {
            performOperation(history_[levelData->levelIndex_], levelData);
            ++levelData->levelIndex_;
        }

        return (levelData->levelIndex_ == levelIndex);
    }

    unsigned int levelCount() const
    {
        return history_.size() + 1;
    }

    void cutHead(const Level *newTop = NULL)
    {
        if(!newTop)
            newTop = &currentLevel_;
        history_.erase(history_.begin() + newTop->levelIndex(),
                       history_.end());
        checkpoints_.erase(checkpoints_.upper_bound(newTop->levelIndex()),
                           checkpoints_.end());
    }
};

} // namespace vigra

#endif // CELLPYRAMID_HXX
