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


#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "graph/debruijnnode.h"
#include "graph/nodecolorer.h"
#include "graph/contiguity.h"
#include "graph/graphscope.h"
#include "graph/viewstate.h"

#include "layout/graphlayout.h"
#include "program/globals.h"

#include <QMainWindow>
#include <QGraphicsScene>
#include <QMap>
#include <QString>
#include <vector>
#include <QLineEdit>
#include <QComboBox>
#include <QLabel>
#include <QRectF>
#include <QThread>

Q_MOC_INCLUDE("graph/debruijnnode.h")

class GraphicsViewZoom;
class PathfinderGraphicsScene;
class DeBruijnNode;
class DeBruijnEdge;
class GraphSearchDialog;
namespace search {
    class GraphSearch;
}
enum UiState {NO_GRAPH_LOADED, GRAPH_LOADED, GRAPH_DRAWN};

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QString fileToLoadOnStartup = "", bool drawGraphAfterLoad = false);
    ~MainWindow() override;

private:
    Ui::MainWindow *ui;
    PathfinderGraphicsScene * m_scene;

    GraphicsViewZoom * m_graphicsViewZoom;
    double m_previousZoomSpinBoxValue;
    QString m_imageFilter;
    QString m_fileToLoadOnStartup;
    bool m_drawGraphAfterLoad;
    UiState m_uiState;
    GraphSearchDialog * m_blastSearchDialog;

    bool m_alreadyShown;
    bool m_rotationMode = false;
    QPointF m_rotationCenter;
    QPointF m_rotationStartPos;
    QByteArray m_rotationBeforeState;

    // Link mode state
    bool m_linkMode = false;
    int m_linkStep = 0;  // 0: not started, 1: waiting for first click, 2: waiting for second click
    DeBruijnNode* m_linkNode1 = nullptr;
    bool m_linkNode1IsTail = false;  // true if tail was clicked, false if head
    DeBruijnNode* m_linkNode2 = nullptr;
    bool m_linkNode2IsTail = false;
    QGraphicsEllipseItem* m_linkMarker1 = nullptr;
    QGraphicsEllipseItem* m_linkMarker2 = nullptr;

    // Edge cap style combo box (added dynamically in constructor)
    QComboBox * m_edgeCapStyleComboBox;
    QLabel * m_edgeCapStyleLabel;

    void cleanUp();
    void displayGraphDetails();
    void clearGraphDetails();
    void resetScene();
    void resetAllNodeColours();
    void drawGraphWithLinePoints(const ViewState &viewState);
    void layoutGraph();
    void zoomToFitRect(QRectF rect);
    void setZoomSpinBoxStep();
    void getSelectedNodeInfo(int & selectedNodeCount, QString & selectedNodeCountText,
                             QString & selectedNodeListText, QString & selectedNodeLengthText, QString &selectedNodeDepthText,
                             QString &selectNodeTagText);
    QString getSelectedEdgeListText();
    std::vector<DeBruijnNode *> getNodesFromLineEdit(QLineEdit * lineEdit, bool exactMatch, std::vector<QString> * nodesNotInGraph = nullptr);
    void setUiState(UiState uiState);
    void selectBasedOnContiguity(ContiguityStatus contiguityStatus);
    void setWidgetsFromSettings();
    QString getDefaultImageFileName();
    void setGraphScopeComboBox(GraphScope graphScope);
    void setupBlastQueryComboBox();
    void setupPathSelectionLineEdit(QLineEdit *lineEdit);
    void setupWalkSelectionLineEdit(QLineEdit *lineEdit);
    bool checkForImageSave();

    void setSelectedNodesWidgetsVisibility(bool visible);
    void setSelectedEdgesWidgetsVisibility(bool visible);
    void setStartingNodesWidgetVisibility(bool visible);
    void setNodeDistanceWidgetVisibility(bool visible);
    void setDepthRangeWidgetVisibility(bool visible);
    void setPathSelectionWidgetVisibility(bool visible);
    void setWalkSelectionWidgetVisibility(bool visible);

    QByteArray captureGraphState() const;
    void pushGraphStateCommand(const QByteArray &before, const QByteArray &after);
    void afterGraphStateRestored(const ViewState &viewState);

    static QByteArray makeStringUrlSafe(QByteArray s);
    std::vector<DeBruijnNode *> addComplementaryNodes(std::vector<DeBruijnNode *> nodes);
    void resetNodeContiguityStatus();

private slots:
    void loadGraph(QString fullFileName = "");
    void loadCSV(QString fullFileName = "");
    void loadLogan(QString accession = "");
    void loadGraphLayout(QString fullFileName = "");
    void loadGraphPaths(QString fullFileName = "");
    void loadGraphLinks(QString fullFileName = "");
    void selectionChanged();
    void graphScopeChanged();
    void drawGraph();
    void zoomSpinBoxChanged();
    void zoomedWithMouseWheel();
    void zoomToFitScene();
    void copySelectedSequencesToClipboardActionTriggered();
    void copySelectedSequencesToClipboard();
    void saveSelectedSequencesToFileActionTriggered();
    void saveSelectedSequencesToFile();
    void copySelectedPathToClipboard();
    void saveSelectedPathToFile();
    void switchColourScheme(int idx = -1);
    void switchTagValue();
    void determineContiguityFromSelectedNode();
    void saveImageCurrentView();
    void saveImageEntireScene();
    void setTextDisplaySettings();
    void fontButtonPressed();
    void setNodeCustomColour();
    void setNodeCustomLabel();
    void setNodeCapStyle();
    void rotateSelectedNodes();
    void onRotationFinished();
    void hideNodes();
    void openSettingsDialog();
    void openAboutDialog();
    void doSelectNodes(const std::vector<DeBruijnNode *> &nodesToSelect,
                       const std::vector<QString> &nodesNotInGraph,
                       bool recolor = false);
    void selectPathNodes();
    void selectWalkNodes();

    void showPathListDialog();
    void showWalkListDialog();
    void selectUserSpecifiedNodes();
    void graphLayoutFinished(const GraphLayout &layout);
    void openBlastSearchDialog();
    void blastChanged();
    void blastQueryChanged();
    void showHidePanels();
    void bringSelectedNodesToFront();
    void selectNodesWithBlastHits();
    void selectNodesWithDeadEnds();
    void selectAll();
    void selectNone();
    void invertSelection();
    void zoomToSelection();
    void selectContiguous();
    void selectMaybeContiguous();
    void selectNotContiguous();
    void openPathfinderUrl();
    void nodeDistanceChanged();
    void depthRangeChanged();
    void afterMainWindowShow();
    void startingNodesExactMatchChanged();
    void openPathSpecifyDialog();
    void nodeWidthChanged();
    void saveEntireGraphToFasta();
    void saveEntireGraphToFastaOnlyPositiveNodes();
    void saveEntireGraphToGfa();
    void saveVisibleGraphToGfa();
    void webBlastSelectedNodes();
    void removeSelection();
    void duplicateSelectedNodes();
    void mergeSelectedNodes();
    void mergeAllPossible();
    void cleanUpAllBlast();
    void changeNodeName();
    void changeNodeDepth();
    void openGraphInfoDialog();
    void exportGraphLayout();
    void openTTTDialog();
    void linkTwoNodes();
    void resetLinkMode();
    void onLinkModeNodeClicked(DeBruijnNode * node, bool isTail);
    void setBackgroundColour();
    void showContextMenu(QPoint pos);

protected:
      void showEvent(QShowEvent *ev) override;

signals:
      void windowLoaded();

};

#endif // MAINWINDOW_H
