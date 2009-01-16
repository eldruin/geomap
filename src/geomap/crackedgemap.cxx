#include "crackedgemap.hxx"

void CrackEdgeMapGenerator::makeCCSymmetric()
{
    vigra::IImage::traverser
        end = crackConnections.lowerRight() - vigra::Diff2D(1, 1),
        row = crackConnections.upperLeft();
    for(; row.y < end.y; ++row.y)
    {
        vigra::IImage::traverser it = row;
        for(; it.x < end.x; ++it.x)
        {
            if(*it & CONN_RIGHT)
                it[vigra::Diff2D(1, 0)] |= CONN_LEFT;
            if(*it & CONN_DOWN)
                it[vigra::Diff2D(0, 1)] |= CONN_UP;
        }
        if(*it & CONN_DOWN)
            it[vigra::Diff2D(0, 1)] |= CONN_UP;
    }

    for(; row.x < end.x; ++row.x)
        if(*row & CONN_RIGHT)
            row[vigra::Diff2D(1, 0)] |= CONN_LEFT;
}

void CrackEdgeMapGenerator::markNodes()
{
    static const unsigned char conn2degree[] =
        {0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4,
         0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 2,
         0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 2};

    vigra::IImage::traverser
        end = crackConnections.lowerRight(),
        row = crackConnections.upperLeft();
    for(; row.y < end.y; ++row.y)
    {
        vigra::IImage::traverser it = row;
        for(; it.x < end.x; ++it.x)
        {
            int conn(*it);

            if(conn2degree[conn] > 2)
                *it = conn | CONN_NODE;
            else if(
                ((conn & CONN_ALL4) == (CONN_RIGHT | CONN_DOWN))
                || (conn & CONN_DIAG))
                *it = conn | CONN_MAYBE_NODE;
        }
    }
}

std::auto_ptr<Vector2Array>
CrackEdgeMapGenerator::followEdge(
    vigra::Point2D &pos,
    vigra::FourNeighborOffsetCirculator &dir)
{
    const vigra::Point2D startPos(pos);
    Vector2 vPos(pos.x - 0.5, pos.y - 0.5);

    std::auto_ptr<Vector2Array> resultPtr(new Vector2Array());
    Vector2Array &result(*resultPtr);
    result.push_back(vPos);

    static const Vector2 dirVector[4] =
        {
            Vector2(1, 0), Vector2(0, -1),
            Vector2(-1, 0), Vector2(0, 1),
        };

    static const int connections[4] =
        { CONN_RIGHT, CONN_UP, CONN_LEFT, CONN_DOWN };

    while(true)
    {
        vPos += dirVector[dir.direction()];
        result.push_back(vPos);
        pos += dir.diff();

        if(pos == startPos)
            break;

        int connection(crackConnections[pos]);
        if(connection & CONN_DIAG)
        {
            using namespace vigra::FourNeighborhood;

            bool turnLeft =
                (connection & CONN_DIAG_UPLEFT
                 ? (dir.direction() == North || dir.direction() == South)
                 : (dir.direction() == East  || dir.direction() == West));

            connection &= ~connections[dir.opposite()];

            if(turnLeft)
                dir.turnLeft();
            else
                dir.turnRight();

            connection &= ~connections[dir.direction()];

            if((connection & CONN_ALL4) == 0)
                connection &= ~CONN_MAYBE_NODE;

            crackConnections[pos] = connection;
            continue;
        }
        else if(connection & CONN_NODE)
            break;

        if(connection & CONN_MAYBE_NODE)
            // we simply pass over it, but we do not want to start a
            // new edge here during further down in the process:
            crackConnections[pos] = connection & ~CONN_MAYBE_NODE;

        dir.turnRight();
        while((connection & connections[dir.direction()]) == 0)
            dir.turnLeft();
    }

    dir.turnRound();
    return resultPtr;
}

void CrackEdgeMapGenerator::followAllEdgesStartingWith(int connMask)
{
    vigra::IImage::traverser
        end = crackConnections.lowerRight(),
        row = crackConnections.upperLeft();
    for(; row.y < end.y; ++row.y)
    {
        vigra::IImage::traverser it = row;
        for(; it.x < end.x; ++it.x)
        {
            if((*it & connMask) == 0)
                continue;

            vigra::Point2D pos(it - crackConnections.upperLeft());

            GeoMap::NodePtr startNode, endNode;

            int startNodeInfo = nodeImage[pos];
            if(startNodeInfo)
                startNode = result->node((startNodeInfo >> 4) - 1);
            else
            {
                startNode = result->addNode(vigra::Vector2(pos.x - 0.5, pos.y - 0.5));
                nodeImage[pos] = startNodeInfo = (startNode->label() + 1) << 4;
            }

            // ATT: duplicate from followEdge()
            static const int connections[4] =
                { CONN_RIGHT, CONN_UP, CONN_LEFT, CONN_DOWN };

            vigra::FourNeighborOffsetCirculator dir;

            do
            {
                int startConn = connections[dir.direction()];
                if((*it & startConn) && !(startNodeInfo & startConn))
                {
                    vigra::Point2D endPos(pos);
                    vigra::FourNeighborOffsetCirculator endDir(dir);
                    
                    std::auto_ptr<Vector2Array> points = followEdge(endPos, endDir);
                    int endConn = connections[endDir.direction()];
                    
                    int endNodeInfo = nodeImage[endPos];
                    if(!endNodeInfo)
                    {
                        endNode = result->addNode(
                            vigra::Vector2(endPos.x - 0.5, endPos.y - 0.5));
                        endNodeInfo = (endNode->label() + 1) << 4;
                    }
                    else
                    {
//                        VIGRA_ASSERT(endNodeInfo & endConn == 0, "double connection?");
                        endNode = result->node((endNodeInfo >> 4) - 1);
                    }

                    GeoMap::EdgePtr edge = result->addEdge(*startNode, *endNode, *points);

                    startNodeInfo |= startConn;
                    if(edge->isLoop())
                    {
                        startNodeInfo |= endConn;
                        nodeImage[pos] = startNodeInfo;
                    }
                    else
                    {
                        nodeImage[pos] = startNodeInfo;
                        nodeImage[endPos] = endNodeInfo | endConn;
                    }
                }
            }
            while(++dir != vigra::FourNeighborCode::InitialDirection);
        }
    }
}
