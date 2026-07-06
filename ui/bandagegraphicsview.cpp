//Copyright 2017 Ryan Wick

//This file is part of Pathfinder

//Pathfinder is free software: you can redistribute it and/or modify
//it under the terms of the GNU General Public License as published by
//the Free Software Foundation, either version 3 of the License, or
//(at your option) any later version.

//Pathfinder is distributed in the hope that it will be useful,
//but WITHOUT ANY WARRANTY; without even the implied warranty of
//MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//GNU General Public License for more details.

//You should have received a copy of the GNU General Public License
//along with Pathfinder.  If not, see <http://www.gnu.org/licenses/>.


#include "bandagegraphicsview.h"
#include "bandagegraphicsscene.h"
#include "graph/graphicsitemnode.h"
#include "graph/debruijnnode.h"
#include "program/globals.h"
#include "program/settings.h"
#include "graphicsviewzoom.h"
#include <QMouseEvent>
#include <QFont>
#include <QMessageBox>
#include <QScrollBar>
#include <QKeySequence>
#include <qmath.h>
#include <cmath>
#include <QUndoStack>

PathfinderGraphicsView::PathfinderGraphicsView(QObject * /*parent*/) :
    QGraphicsView(), m_rotation(0.0)
{
    setDragMode(QGraphicsView::RubberBandDrag);
    setAntialiasing(g_settings->antialiasing);
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    setBackgroundBrush(QBrush(Qt::white));
}



void PathfinderGraphicsView::mousePressEvent(QMouseEvent * event)
{
    // Ctrl key or Middle button = scroll hand drag
    if (event->modifiers() == Qt::CTRL || event->button() == Qt::MiddleButton) {
        setDragMode(QGraphicsView::ScrollHandDrag);
        m_previousPos = event->pos();
        QGraphicsView::mousePressEvent(event);
        return;
    }

    // Right-click: never let QGraphicsView change selection.
    // The context menu will be shown via customContextMenuRequested signal.
    if (event->button() == Qt::RightButton) {
        m_previousPos = event->pos();
        // Do NOT call QGraphicsView::mousePressEvent — this preserves selection.
        return;
    }

    // In rotation mode, capture left button for rotation drag
    if (g_rotationMode && event->button() == Qt::LeftButton) {
        m_previousPos = event->pos();
        // Do NOT pass to QGraphicsView — we handle rotation ourselves
        return;
    }

    // In link mode, capture left button for node endpoint selection
    if (g_linkMode && event->button() == Qt::LeftButton) {
        qDebug() << "Link mode: mouse press detected";
        m_previousPos = event->pos();
        // Find the clicked node and determine head/tail
        QPointF scenePos = mapToScene(event->pos());
        QGraphicsItem *item = scene()->itemAt(scenePos, transform());
        GraphicsItemNode *gin = dynamic_cast<GraphicsItemNode *>(item);
        if (gin) {
            DeBruijnNode *node = gin->m_deBruijnNode;
            qDebug() << "Clicked node:" << node->getName();
            // Determine if click is closer to head (start) or tail (end)
            QPointF first = gin->getFirst();
            QPointF last = gin->getLast();
            double distToFirst = distance(scenePos.x(), scenePos.y(), first.x(), first.y());
            double distToLast = distance(scenePos.x(), scenePos.y(), last.x(), last.y());
            bool isTail = (distToLast < distToFirst);
            qDebug() << "isTail =" << isTail;
            emit linkModeNodeClicked(node, isTail);
        } else {
            qDebug() << "No node clicked";
        }
        // Do NOT pass to QGraphicsView — we handle link mode ourselves
        return;
    }

    m_previousPos = event->pos();
    QGraphicsView::mousePressEvent(event);
}

void PathfinderGraphicsView::mouseReleaseEvent(QMouseEvent * event)
{
    // For middle button: reset drag mode, but also send the release event
    // so QGraphicsView can finalize the scroll.
    if (event->button() == Qt::MiddleButton) {
        QGraphicsView::mouseReleaseEvent(event);
        setDragMode(QGraphicsView::RubberBandDrag);
        return;
    }

    // Exit rotation mode on left button release
    if (g_rotationMode && event->button() == Qt::LeftButton) {
        g_rotationMode = false;
        setCursor(Qt::ArrowCursor);
        emit rotationFinished();
        return;
    }

    QGraphicsView::mouseReleaseEvent(event);
    setDragMode(QGraphicsView::RubberBandDrag);
    g_settings->nodeDragging = NEARBY_PIECES;
}

void PathfinderGraphicsView::mouseMoveEvent(QMouseEvent * event)
{
    // Middle-button drag = pan (same as Ctrl+Left drag)
    if (event->buttons() & Qt::MiddleButton) {
        QPointF delta = event->pos() - m_previousPos;
        m_previousPos = event->pos();

        // Simulate scroll hand drag manually
        QScrollBar *hBar = horizontalScrollBar();
        QScrollBar *vBar = verticalScrollBar();
        if (hBar)
            hBar->setValue(hBar->value() - int(delta.x()));
        if (vBar)
            vBar->setValue(vBar->value() - int(delta.y()));

        g_settings->nodeDragging = NO_DRAGGING;
        return;
    }

    // Rotation mode: rotate selected nodes around center
    if (g_rotationMode && (event->buttons() & Qt::LeftButton)) {
        QPointF viewCentre = mapFromScene(g_rotationCenter);
        double angle = angleBetweenTwoLines(viewCentre, m_previousPos, viewCentre, event->pos());

        // Determine rotation direction using cross product
        QPointF v1 = m_previousPos - viewCentre;
        QPointF v2 = event->pos() - viewCentre;
        double cross = v1.x() * v2.y() - v1.y() * v2.x();
        if (cross < 0)
            angle = -angle;

        // Apply rotation to all selected nodes
        auto *bgScene = dynamic_cast<PathfinderGraphicsScene *>(scene());
        if (bgScene) {
            std::vector<GraphicsItemNode *> selectedNodes = bgScene->getSelectedGraphicsItemNodes();
            for (auto *node : selectedNodes) {
                node->rotatePoints(g_rotationCenter, angle);
            }
            if (!selectedNodes.empty())
                selectedNodes.front()->fixEdgePaths(&selectedNodes);
        }

        m_previousPos = event->pos();
        g_settings->nodeDragging = NO_DRAGGING;
        return;
    }

    //If the user drags the right mouse button while holding control,
    //the view rotates.
    bool rightButtonDown = event->buttons() & Qt::RightButton;
    if (event->modifiers() == Qt::CTRL && rightButtonDown)
    {
        QPointF viewCentre(width() / 2.0, height() / 2.0);
        double angle = angleBetweenTwoLines(viewCentre, m_previousPos, viewCentre, event->pos());
        angle *= 57.295779513; //convert to degrees

        rotate(angle);
        m_rotation += angle;

        m_previousPos = event->pos();

        g_settings->nodeDragging = NO_DRAGGING;
    }
    else
    {
        QGraphicsView::mouseMoveEvent(event);
    }
}


void PathfinderGraphicsView::mouseDoubleClickEvent(QMouseEvent * event)
{
    //Find the node beneath the cursor.
    QGraphicsItem * item = itemAt(event->pos());

    auto * graphicsItemNode = dynamic_cast<GraphicsItemNode *>(item);
    if (graphicsItemNode != nullptr)
        emit doubleClickedNode(graphicsItemNode->m_deBruijnNode);
}




void PathfinderGraphicsView::keyPressEvent(QKeyEvent * event)
{
    QGraphicsView::keyPressEvent(event);
    g_undoStack->setActive(false);
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace)
    {
        event->accept();
        g_undoStack->setActive(true);
    }
}


void PathfinderGraphicsView::setAntialiasing(bool antialiasingOn)
{
    if (antialiasingOn)
        setRenderHint(QPainter::Antialiasing, true);
    else
        setRenderHint(QPainter::Antialiasing, false);
}


void PathfinderGraphicsView::setRotation(double newRotation)
{
    rotate(newRotation - m_rotation);
    m_rotation = newRotation;
}

void PathfinderGraphicsView::changeRotation(double rotationChange)
{
    m_rotation += rotationChange;
}

void PathfinderGraphicsView::undoRotation()
{
    rotate(-m_rotation);
    m_rotation = 0.0;
}



// This function will return true if a point is visible in the viewport.
bool PathfinderGraphicsView::isPointVisible(QPointF p)
{
    QPointF corner1, corner2, corner3, corner4;
    getFourViewportCornersInSceneCoordinates(&corner1, &corner2, &corner3, &corner4);

    //If both the point is on the same side of all the viewport boundaries,
    //then it is on the inside of the viewport.
    bool side1 = sideOfLine(p, QLineF(corner1, corner2));
    bool side2 = sideOfLine(p, QLineF(corner2, corner3));
    bool side3 = sideOfLine(p, QLineF(corner3, corner4));
    bool side4 = sideOfLine(p, QLineF(corner4, corner1));

    return (side1 == side2 && side2 == side3 && side3 == side4);
}


//This function should be used when a line segment might be entirely visible,
//entirely invisible or partially visible.
QPointF PathfinderGraphicsView::findIntersectionWithViewportBoundary(QLineF line)
{
    QPointF corner1, corner2, corner3, corner4;
    getFourViewportCornersInSceneCoordinates(&corner1, &corner2, &corner3, &corner4);

    QLineF viewportEdge1(corner1, corner2);
    QLineF viewportEdge2(corner2, corner3);
    QLineF viewportEdge3(corner3, corner4);
    QLineF viewportEdge4(corner4, corner1);

    QPointF intersectionPoint;
    if (line.intersects(viewportEdge1, &intersectionPoint) == QLineF::BoundedIntersection)
        return intersectionPoint;
    if (line.intersects(viewportEdge2, &intersectionPoint) == QLineF::BoundedIntersection)
        return intersectionPoint;
    if (line.intersects(viewportEdge3, &intersectionPoint) == QLineF::BoundedIntersection)
        return intersectionPoint;
    if (line.intersects(viewportEdge4, &intersectionPoint) == QLineF::BoundedIntersection)
        return intersectionPoint;

    //The code shouldn't get here.
    return {};
}


// This function should be used when a line segment might be entirely visible
// (both end points visible), entirely invisible (both end points invisible
// and no part of the line is visible) or partially visible (both end points
// invisible, but part of the line is visible).  It will return the visible
// part of the line.  If the line is entirely invisible, it returns an empty
// QLineF and success becomes false.
QLineF PathfinderGraphicsView::findVisiblePartOfLine(QLineF line, bool * success)
{
    bool p1visible = isPointVisible(line.p1());
    bool p2visible = isPointVisible(line.p2());

    //If both points are visible, we just return the line.
    if (p1visible && p2visible)
    {
        *success = true;
        return line;
    }

    //If both points are not visible, we check to see if the line crosses
    //the view port region.
    if (!p1visible && !p2visible)
    {
        QPointF corner1, corner2, corner3, corner4;
        getFourViewportCornersInSceneCoordinates(&corner1, &corner2, &corner3, &corner4);

        QLineF viewportEdge1(corner1, corner2);
        QLineF viewportEdge2(corner2, corner3);
        QLineF viewportEdge3(corner3, corner4);
        QLineF viewportEdge4(corner4, corner1);

        QPointF intersectionPoint1, intersectionPoint2;
        bool intersection1 = line.intersects(viewportEdge1, &intersectionPoint1) == QLineF::BoundedIntersection;
        bool intersection2 = line.intersects(viewportEdge2, &intersectionPoint2) == QLineF::BoundedIntersection;
        bool intersection3 = line.intersects(viewportEdge3, &intersectionPoint1) == QLineF::BoundedIntersection;
        bool intersection4 = line.intersects(viewportEdge4, &intersectionPoint2) == QLineF::BoundedIntersection;

        if ((int(intersection1) + int(intersection2) + int(intersection3) + int(intersection4)) == 2)
        {
            *success = true;
            return QLineF(intersectionPoint1, intersectionPoint2);
        }

        *success = false;
        return {};
    }

    //If we get here, then one point is visible and the other is not.
    //Find the point that is within the viewport that lies on the edge
    //of the line.
    QPointF intersection = findIntersectionWithViewportBoundary(line);
    if (p1visible)
    {
        *success = true;
        return QLineF(line.p1(), intersection);
    }
    else
    {
        *success = true;
        return QLineF(line.p2(), intersection);
    }
}



// This function gets the four corners of the viewport in scene coordinates.
// The corners are in order: top-left, top-right, bottom-right, bottom-left.
void PathfinderGraphicsView::getFourViewportCornersInSceneCoordinates(QPointF * c1, QPointF * c2, QPointF * c3, QPointF * c4)
{
    QPointF p1 = mapToScene(0, 0);
    QPointF p2 = mapToScene(viewport()->width(), 0);
    QPointF p3 = mapToScene(0, viewport()->height());
    QPointF p4 = mapToScene(viewport()->width(), viewport()->height());

    //Order the points into: top-left, top-right, bottom-right, bottom-left
    QPointF topLeft = QPointF(std::min(p1.x(), p3.x()), std::min(p1.y(), p2.y()));
    QPointF topRight = QPointF(std::max(p1.x(), p3.x()), std::min(p1.y(), p2.y()));
    QPointF bottomRight = QPointF(std::max(p1.x(), p3.x()), std::max(p1.y(), p2.y()));
    QPointF bottomLeft = QPointF(std::min(p1.x(), p3.x()), std::max(p1.y(), p2.y()));

    *c1 = topLeft;
    *c2 = topRight;
    *c3 = bottomRight;
    *c4 = bottomLeft;
}

double PathfinderGraphicsView::distance(double x1, double y1, double x2, double y2)
{
    double xDiff = x1 - x2;
    double yDiff = y1 - y2;
    return sqrt(xDiff * xDiff + yDiff * yDiff);
}

double PathfinderGraphicsView::angleBetweenTwoLines(QPointF line1Start, QPointF line1End, QPointF line2Start, QPointF line2End)
{
    double line1XDiff = line1End.x() - line1Start.x();
    double line1YDiff = line1End.y() - line1Start.y();
    double line1Length = sqrt(line1XDiff * line1XDiff + line1YDiff * line1YDiff);

    double line2XDiff = line2End.x() - line2Start.x();
    double line2YDiff = line2End.y() - line2Start.y();
    double line2Length = sqrt(line2XDiff * line2XDiff + line2YDiff * line2YDiff);

    double numerator = line1XDiff * line2XDiff + line1YDiff * line2YDiff;
    double denominator = line1Length * line2Length;
    double cosine = numerator / denominator;

    //Just in case, make sure cosine is in the range [-1, 1]
    if (cosine > 1.0)
        cosine = 1.0;
    if (cosine < -1.0)
        cosine = -1.0;

    return acos(cosine);
}

bool PathfinderGraphicsView::differentSidesOfLine(QPointF p1, QPointF p2, QLineF line)
{
    return sideOfLine(p1, line) != sideOfLine(p2, line);
}

bool PathfinderGraphicsView::differentSidesOfLine(QPointF p1, QPointF p2, QPointF p3, QPointF p4, QLineF line)
{
    bool side1 = sideOfLine(p1, line);
    bool side2 = sideOfLine(p2, line);
    bool side3 = sideOfLine(p3, line);
    bool side4 = sideOfLine(p4, line);

    return (side1 != side2 || side1 != side3 || side1 != side4);
}

bool PathfinderGraphicsView::sideOfLine(QPointF p, QLineF line)
{
    //If the line is almost vertical, just check the X values.
    if (abs(line.dx()) < 0.00000001)
        return p.x() < line.x1();

    //If the line is almost horizontal, just check the Y values.
    if (abs(line.dy()) < 0.00000001)
        return p.y() < line.y1();

    double lineSlope = line.dy() / line.dx();
    double lineIntercept = line.y1() - lineSlope * line.x1();
    double pointIntercept = p.y() - lineSlope * p.x();

    return pointIntercept < lineIntercept;
}
