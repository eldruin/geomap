#ifndef CELLPYRAMID_HXX
#define CELLPYRAMID_HXX

#include <vector>
#include <map>

/* Known design weaknesses:
 * - cutApex() calls storeCheckpoint to restore nextCheckpointLevelIndex_
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

    enum OperationType { RemoveIsolatedNode,
                         MergeFaces,
                         RemoveBridge,
                         MergeEdges,
                         // composed Operations:
                         RemoveEdge,
                         RemoveEdgeWithEnds,
                         Composite };

    struct Operation
    {
        OperationType type;
        union
        {
            typename DartTraverser::Serialized param;
            std::vector<Operation>            *opList;
        };

        Operation(OperationType opType, DartTraverser paramDart)
        : type(opType), param(paramDart.serialize())
        {}

            // FIXME: maybe this should not be the default constructor?
        Operation()
        : type(Composite), opList(new std::vector<Operation>)
        {}

        Operation(const Operation &other)
        {
            assign(other);
        }

        Operation &operator =(const Operation &other)
        {
            if(type == Composite)
                ;//delete opList; // FIXME: memory leak! (but crashes)
            assign(other);
            return *this;
        }

        ~Operation()
        {
            if(type == Composite)
                delete opList;
        }

      private:
            // deep copy, expects *this to be uninitialized
        void assign(const Operation &other)
        {
            if((type = other.type) == Composite)
                opList = new History(other.opList->begin(), other.opList->end());
            else
                param = other.param;
        }
    }; // struct Operation

    class Level
    {
      public:
        typedef CellPyramid<Segmentation, CellStatistics> Pyramid;
        // using this typedef for the friend decl. results in:
        // error: using typedef-name `vigra::CellPyramid<SEGMENTATION, CELLSTATISTICS>::Level::Pyramid' after `class'
        friend class CellPyramid<Segmentation, CellStatistics>;

        const unsigned int index() const
            { return index_; }
        const Segmentation &segmentation() const
            { return segmentation_; }
        const CellStatistics &cellStatistics() const
            { return cellStatistics_; }
        const Pyramid *pyramid() const
            { return pyramid_; }

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

      protected:
        Level(const Segmentation &s,
              const CellStatistics &c,
              const Pyramid *p)
        : index_(0), subIndex_(0), segmentation_(s), cellStatistics_(c), pyramid_(p)
        {}

            /** Returns false (and does not change this level) if the given
             * levelData is a "better" position for reaching levelIndex
             * than the last checkpoint, that is:
             *
             * lastCheckpointIt->first < levelData->LevelIndex() < levelIndex
             */
        bool gotoLastCheckpointBefore(unsigned int levelIndex)
        {
            typedef typename Pyramid::CheckpointMap CheckpointMap;
            typename CheckpointMap::const_iterator lastCheckpointIt =
                pyramid_->checkpoints_.upper_bound(levelIndex);
            --lastCheckpointIt;

            if((index() <= levelIndex) && (lastCheckpointIt->first <= index()))
                return false;

            std::cerr << "to get from level " << index() << " to " << levelIndex
                      << ", we use checkpoint " << lastCheckpointIt->first
                      << "  (subindex " << lastCheckpointIt->second.subIndex_
                     << ")\n";

            operator=(lastCheckpointIt->second);
            return true;
        }

        FaceInfo &removeIsolatedNodeInternal(const DartTraverser & dart)
        {
            cellStatistics_.preRemoveIsolatedNode(dart);
            FaceInfo &result(segmentation_.removeIsolatedNode(dart));
            cellStatistics_.postRemoveIsolatedNode(result);
            ++subIndex_;
            return result;
        }

        FaceInfo &mergeFacesInternal(const DartTraverser & dart)
        {
            cellStatistics_.preMergeFaces(dart);
            FaceInfo &result(segmentation_.mergeFaces(dart));
            cellStatistics_.postMergeFaces(result);
            ++subIndex_;
            return result;
        }

        FaceInfo &removeBridgeInternal(const DartTraverser & dart)
        {
            cellStatistics_.preRemoveBridge(dart);
            FaceInfo &result(segmentation_.removeBridge(dart));
            cellStatistics_.postRemoveBridge(result);
            ++subIndex_;
            return result;
        }

        EdgeInfo &mergeEdgesInternal(const DartTraverser & dart)
        {
            cellStatistics_.preMergeEdges(dart);
            EdgeInfo &result(segmentation_.mergeEdges(dart));
            cellStatistics_.postMergeEdges(result);
            ++subIndex_;
            return result;
        }

        CellInfo &performOperation(const Operation &op)
        {
            if(op.type == Composite)
            {
                CellInfo *result = NULL;
                for(typename History::iterator it = op.opList->begin();
                    it != op.opList->end(); ++it)
                    result = &performOperation(*it);
                return *result;
            }

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
              case Composite:
                  ;
            }

            std::cerr << "Unknown operation type " << op.type
                      << " in CellPyramid<>::performOperation!\n";
            vigra_fail("Unknown operation type in CellPyramid<>::performOperation!");
            return segmentation_.face(0);
        }

        unsigned int   index_, subIndex_;
        Segmentation   segmentation_;
        CellStatistics cellStatistics_;
        const Pyramid *pyramid_;
    }; // class Level

  protected:
    friend class CellPyramid<Segmentation, CellStatistics>::Level;

    typedef std::map<unsigned int, Level>
        CheckpointMap;
    typedef std::vector<Operation>
        History;

    CheckpointMap checkpoints_;
    History       history_;
    Level         topLevel_;
    unsigned int  nextCheckpointLevelIndex_;
    unsigned int  composing_;

    CellInfo &addAndPerformOperation(OperationType t, const DartTraverser &p)
    {
//         vigra_precondition(topLevel_.index() == levelCount()-1,
//             "addAndPerformOperation(): topLevel_ is not the top level anymore");

        CellInfo *result = NULL;
        if(!composing_)
        {
            try
            {
                history_.push_back(Operation(t, p));
                result = &topLevel_.performOperation(history_.back());
                ++topLevel_.index_;
                if(topLevel_.subIndex_ >= nextCheckpointLevelIndex_)
                    storeCheckpoint(topLevel_);
            }
            catch(...)
            {
                cutAbove(topLevel_.index());
                throw;
            }
        }
        else
        {
            History &opList(*history_.back().opList);
            try
            {
                opList.push_back(Operation(t, p));
                result = &topLevel_.performOperation(opList.back());
            }
            catch(...)
            {
                opList.pop_back();
                throw;
            }
        }
        return *result;
    }

        // called after adjusting topLevel_ to remove levels above
    void cutApex()
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
        unsigned int totalCellCount =
            level.segmentation().nodeCount() +
            level.segmentation().edgeCount() +
            level.segmentation().faceCount();
        nextCheckpointLevelIndex_ =
            level.subIndex_ + std::max(totalCellCount / 4, (unsigned)10);

        if(!checkpoints_.count(level.index()))
        {
            checkpoints_.insert(std::make_pair(level.index(), level));

            std::cerr << "--- stored checkpoint at level " << level.index()
                      << " (subindex " << level.subIndex_
                      << "), next scheduled for subindex "
                      << nextCheckpointLevelIndex_
                      << " (" << totalCellCount << " cells total left) ---\n";
        }
    }

    CellPyramid(const Segmentation &level0,
                const CellStatistics &level0Stats = CellStatistics())
    : topLevel_(level0, level0Stats, this),
      nextCheckpointLevelIndex_(0),
      composing_(0)
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

    void beginComposite()
    {
        if(!composing_)
            history_.push_back(Operation());
        ++composing_;
    }

    void changeIntoComposite()
    {
        if(!composing_)
        {
            Operation &last(history_.back());
            Operation tmp(last);         // copy old
            last = Operation();          // create composite
            last.opList->push_back(tmp); // and re-fill with old
        }
        ++composing_;
    }

    void endComposite()
    {
        if(!--composing_)
        {
            Operation &last(history_.back());
            if(last.opList->size() == 1)
            {
                History *h = last.opList;
                last = (*h)[0];
                delete h;
            }
            ++topLevel_.index_;
            if(topLevel_.subIndex_ >= nextCheckpointLevelIndex_)
                storeCheckpoint(topLevel_);
        }
    }

    Level &topLevel()
    {
        return topLevel_;
    }

    const Level &topLevel() const
    {
        return topLevel_;
    }

    Level *getLastCheckpointBefore(unsigned int levelIndex) const
    {
        vigra_precondition(levelIndex < levelCount(),
            "getLevel/getLastCheckpointBefore(): invalid level index given");
        typename CheckpointMap::const_iterator lastCheckpointIt =
            checkpoints_.upper_bound(levelIndex);
        --lastCheckpointIt;
        return new Level(lastCheckpointIt->second);
    }

    Level *getLevel(unsigned int levelIndex) const
    {
        Level *result = getLastCheckpointBefore(levelIndex);
        result->gotoLevel(levelIndex);
        return result;
    }

    unsigned int levelCount() const
    {
        return history_.size() + 1;
    }

    typedef typename History::const_iterator
        HistoryIterator;

    HistoryIterator historyBegin() const
    {
        return history_.begin();
    }

    HistoryIterator historyEnd() const
    {
        return history_.end();
    }

    void cutAbove(const Level &level)
    {
        if(topLevel_.index() != levelCount()-1)
        {
            std::cerr << "topLevel_.index() == " << topLevel_.index()
                      << " but levelCount() == " << levelCount() << "\n";
            vigra_precondition(topLevel_.index() == levelCount()-1,
                               "cutAbove(): topLevel_ is not the top level anymore");
        }
        if(topLevel_.index() != level.index())
        {
            topLevel_ = level;
            cutApex();
        }
    }

    void cutAbove(unsigned int levelIndex)
    {
        if(topLevel_.index() != levelCount()-1)
        {
            std::cerr << "topLevel_.index() == " << topLevel_.index()
                      << " but levelCount() == " << levelCount() << "\n";
            vigra_precondition(topLevel_.index() == levelCount()-1,
                               "cutAbove(): topLevel_ is not the top level anymore");
        }
        if(topLevel_.index() > levelIndex)
        {
            topLevel_.gotoLevel(levelIndex);
            cutApex();
        }
    }
};

} // namespace vigra

#endif // CELLPYRAMID_HXX
