// Copyright 2017 Ryan Wick
// Copyright 2022 Anton Korobeynikov

//This file is part of Pathfinder

// Pathfinder is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// Pathfinder is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with Pathfinder.  If not, see <http://www.gnu.org/licenses/>.

#include "graphsearchdialog.h"
#include "ui_graphsearchdialog.h"

#include "graphsearch/hit.h"
#include "graphsearch/query.h"
#include "graphsearch/graphsearch.h"

#include "graph/debruijnnode.h"
#include "graph/assemblygraph.h"
#include "graph/annotationsmanager.h"

#include "querypathsdialog.h"
#include "hitfiltersdialog.h"

#include "program/globals.h"
#include "program/settings.h"
#include "program/memory.h"

#include <QApplication>
#include <QFileDialog>
#include <QFile>
#include <QString>
#include <QMessageBox>
#include <QCheckBox>
#include <QColorDialog>
#include <QPainter>
#include <QSortFilterProxyModel>
#include <QRegularExpression>
#include <QTextStream>

using namespace search;

enum class QueriesHitColumns : unsigned {
    Color = 0,
    Show = 1,
    QueryName = 2,
    Type = 3,
    Length = 4,
    Hits = 5,
    QueryCover = 6,
    Paths = 7,
    TotalHitColumns = Paths + 1
};

enum class HitsColumns : unsigned {
    Color = 0,
    QueryName = 1,
    NodeName = 2,
    PercentIdentity = 3,
    AlignmentLength = 4,
    QueryCover = 5,
    Mismatches = 6,
    GapOpens = 7,
    QueryStart = 8,
    QueryEnd = 9,
    NodeStart = 10,
    NodeEnd = 11,
    Evalue = 12,
    BitScore = 13,
    TotalHitColumns = BitScore + 1
};

GraphSearchDialog::GraphSearchDialog(QWidget *parent, const QString& autoQuery)
 : QDialog(parent), ui(new Ui::GraphSearchDialog) {
    ui->setupUi(this);

    m_graphSearch = search::GraphSearch::get(search::BLAST, QDir::temp(), this);

    setWindowFlags(windowFlags() | Qt::Tool | Qt::WindowMinMaxButtonsHint);

    m_queriesListModel = new QueriesListModel(m_graphSearch->queries(),
                                              ui->blastQueriesTable);
    auto *proxyQModel = new QSortFilterProxyModel(ui->blastQueriesTable);
    proxyQModel->setSourceModel(m_queriesListModel);
    ui->blastQueriesTable->setModel(proxyQModel);
    ui->blastQueriesTable->setSortingEnabled(true);

    auto *queryPathsDelegate = new PathButtonDelegate(ui->blastQueriesTable);
    ui->blastQueriesTable->setItemDelegateForColumn(int(QueriesHitColumns::Paths),
                                                    queryPathsDelegate);
    connect(queryPathsDelegate, &PathButtonDelegate::queryPathSelectionChanged,
            [this] { emit queryPathSelectionChanged(); });

    m_hitsListModel = new HitsListModel(m_graphSearch->queries(), ui->blastHitsTable);
    auto *proxyHModel = new QSortFilterProxyModel(ui->blastHitsTable);
    proxyHModel->setSourceModel(m_hitsListModel);
    ui->blastHitsTable->setModel(proxyHModel);
    ui->blastHitsTable->setSortingEnabled(true);

    setFilterText();

    // If the dialog is given an autoQuery parameter, import results directly
    if (!autoQuery.isEmpty()) {
        importResultsFromFile(autoQuery);
        QMetaObject::invokeMethod(this, "close", Qt::QueuedConnection);
        emit changed();
        return;
    }

    connect(ui->importResultsButton, SIGNAL(clicked()), this, SLOT(importResultsButtonClicked()));
    connect(ui->filtersButton, SIGNAL(clicked()), this, SLOT(openFiltersDialog()));
    connect(ui->closeButton, SIGNAL(clicked()), this, SLOT(accept()));

    // Select All and Invert Selection buttons for queries
    connect(ui->selectAllQueriesButton, &QPushButton::clicked, [this]() {
        auto &queries = m_graphSearch->queries();
        for (size_t i = 0; i < queries.getQueryCount(); ++i) {
            if (auto *query = queries[i]) {
                query->setShown(true);
            }
        }
        updateTables();
        emit changed();
    });

    connect(ui->invertSelectionQueriesButton, &QPushButton::clicked, [this]() {
        auto &queries = m_graphSearch->queries();
        for (size_t i = 0; i < queries.getQueryCount(); ++i) {
            if (auto *query = queries[i]) {
                query->setShown(!query->isShown());
            }
        }
        updateTables();
        emit changed();
    });

    // Selection change handler (for future use)
    connect(ui->blastQueriesTable->selectionModel(),
        &QItemSelectionModel::selectionChanged,
        [this]() {
            // Could enable/disable buttons here if needed
        });

    connect(ui->blastQueriesTable,
            &QTableView::clicked,
            this,
            [this, proxyQModel](const QModelIndex &proxyIndex) {
                if (!proxyIndex.isValid())
                    return;

                auto sourceIndex = proxyQModel->mapToSource(proxyIndex);
                auto column = QueriesHitColumns(sourceIndex.column());
                if (column != QueriesHitColumns::Color)
                    return;

                if (auto *query = m_queriesListModel->query(sourceIndex)) {
                    QColor chosenColour = QColorDialog::getColor(query->getColour(),
                                                                 this,
                                                                 "Query color", QColorDialog::ShowAlphaChannel);
                    if (!chosenColour.isValid())
                        return;

                    m_queriesListModel->setColor(sourceIndex, chosenColour);
                    this->activateWindow();
                }
            });

    connect(m_queriesListModel, &QueriesListModel::dataChanged,
            [this]() {
                emit changed();
            });

    // Propagate data changes to the queries proxy model
    connect(m_queriesListModel, &QueriesListModel::dataChanged,
            [proxyQModel](const QModelIndex &topLeft, const QModelIndex &bottomRight) {
                emit proxyQModel->dataChanged(proxyQModel->mapFromSource(topLeft), proxyQModel->mapFromSource(bottomRight));
            });

    // When queries change, update the hits table too
    connect(m_queriesListModel, &QueriesListModel::dataChanged,
            [this, proxyHModel]() {
                m_hitsListModel->update(m_graphSearch->queries());
                emit proxyHModel->dataChanged(QModelIndex(), QModelIndex());
            });

    connect(ui->blastQueriesTable, &QTableView::doubleClicked,
            [this](const QModelIndex &index) {
                if (!index.isValid())
                    return;

                auto *proxyModel = qobject_cast<QSortFilterProxyModel *>(ui->blastQueriesTable->model());
                auto sourceIndex = proxyModel->mapToSource(index);
                auto *query = m_queriesListModel->query(sourceIndex);
                if (!query || query->getPaths().empty())
                    return;

                QueryPathsDialog dialog(query, this);
                dialog.exec();
                emit queryPathSelectionChanged();
            });
}

GraphSearchDialog::~GraphSearchDialog() {
    delete ui;
}

void GraphSearchDialog::afterWindowShow() {
    // Nothing to do in simplified mode
}

void GraphSearchDialog::clearHits() {
    m_graphSearch->clearHits();
    m_hitsListModel->clear();
    updateTables();
    emit changed();
}

void GraphSearchDialog::setFilterText() {
    QStringList filters;
    if (g_settings->blastAlignmentLengthFilter.on)
        filters << QString("Aln. length ≥%1").arg(int(g_settings->blastAlignmentLengthFilter));
    if (g_settings->blastQueryCoverageFilter.on)
        filters << QString("Query coverage ≥%1%").arg(double(g_settings->blastQueryCoverageFilter));
    if (g_settings->blastIdentityFilter.on)
        filters << QString("Identity ≥%1%").arg(double(g_settings->blastIdentityFilter));
    if (g_settings->blastEValueFilter.on)
        filters << QString("E-value ≤%1").arg(g_settings->blastEValueFilter.val.asString(false));
    if (g_settings->blastBitScoreFilter.on)
        filters << QString("Bit score ≥%1").arg(double(g_settings->blastBitScoreFilter));

    if (filters.empty())
        ui->filtersLabel->setText("Current filters: None");
    else
        ui->filtersLabel->setText("Current filters: " + filters.join(", "));
}

void GraphSearchDialog::updateTables() {
    m_queriesListModel->update();
    m_hitsListModel->update(m_graphSearch->queries());
}

void GraphSearchDialog::updateTablesAndEmit() {
    updateTables();
    emit changed();
}

void GraphSearchDialog::importResultsButtonClicked() {
    QStringList fileNames = QFileDialog::getOpenFileNames(
        this,
        "Import Search Results",
        g_memory->rememberedPath,
        "All supported formats (*.paf *.blast *.out *.domtbl *.tbl);;"
        "PAF files (*.paf);;"
        "BLAST tabular (*.blast *.out);;"
        "HMMER domtbl (*.domtbl *.tbl);;"
        "All files (*)");

    if (fileNames.isEmpty())
        return;

    int totalHitsImported = 0;
    for (const QString &fullFileName : fileNames) {
        totalHitsImported += importResultsFromFile(fullFileName);
    }

    if (!fileNames.isEmpty()) {
        g_memory->rememberedPath = QFileInfo(fileNames.first()).absolutePath();
    }
}

int GraphSearchDialog::importResultsFromFile(const QString &fullFileName) {
    // Auto-detect format from extension
    QString ext = QFileInfo(fullFileName).suffix().toLower();
    int hitsImported = 0;

    if (ext == "paf") {
        hitsImported = importPAF(fullFileName);
    } else if (ext == "blast" || ext == "out") {
        hitsImported = importBlastTabular(fullFileName);
    } else if (ext == "domtbl" || ext == "tbl") {
        hitsImported = importHmmerDomtbl(fullFileName);
    } else {
        // Try PAF first, then BLAST
        hitsImported = importPAF(fullFileName);
        if (hitsImported == 0) {
            hitsImported = importBlastTabular(fullFileName);
        }
    }

    if (hitsImported > 0) {
        // Set annotation group name
        m_graphSearch->setAnnotationGroupName("Graph Search Hits");

        m_graphSearch->queries().findQueryPaths();
        m_graphSearch->queries().searchOccurred();
        updateTables();
        emit changed();

        QString baseName = QFileInfo(fullFileName).fileName();
        ui->importStatusLabel->setText(
            QString("Imported %1 hits from %2")
                .arg(hitsImported)
                .arg(baseName));
    } else {
        QMessageBox::warning(this, "Import Failed",
            "No valid hits found in the file. Please check the format.");
        ui->importStatusLabel->setText("Import failed");
    }

    return hitsImported;
}

// Helper function to find a node by name, trying with/without +/- suffix
static DeBruijnNode* findNodeByName(const QString &name) {
    // Try exact name first
    auto it = g_assemblyGraph->m_deBruijnGraphNodes.find(name.toStdString());
    if (it != g_assemblyGraph->m_deBruijnGraphNodes.end())
        return *it;

    // Try with + suffix
    QString posName = name + "+";
    it = g_assemblyGraph->m_deBruijnGraphNodes.find(posName.toStdString());
    if (it != g_assemblyGraph->m_deBruijnGraphNodes.end())
        return *it;

    // Try with - suffix
    QString negName = name + "-";
    it = g_assemblyGraph->m_deBruijnGraphNodes.find(negName.toStdString());
    if (it != g_assemblyGraph->m_deBruijnGraphNodes.end())
        return *it;

    return nullptr;
}

int GraphSearchDialog::importPAF(const QString &fullFileName) {
    QFile file(fullFileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return 0;

    Queries &queries = m_graphSearch->queries();
    int hitsImported = 0;

    QTextStream in(&file);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#'))
            continue;

        QStringList parts = line.split('\t');
        if (parts.size() < 12)
            continue;

        // PAF format:
        // 0: queryName, 1: queryLen, 2: queryStart, 3: queryEnd
        // 4: strand, 5: targetName, 6: targetLen, 7: targetStart, 8: targetEnd
        // 9: residues, 10: alnLen, 11: mapQ
        QString queryName = parts[0];
        int queryLen = parts[1].toInt();
        int queryStart = parts[2].toInt() + 1; // Convert to 1-based
        int queryEnd = parts[3].toInt();
        // bool strand = (parts[4] == "+"); // unused for now
        QString targetName = parts[5];
        // int targetLen = parts[6].toInt(); // unused
        int targetStart = parts[7].toInt() + 1; // Convert to 1-based
        int targetEnd = parts[8].toInt();
        int alnLen = parts[10].toInt();

        // Get or create query
        Query *query = queries.getQueryFromName(queryName);
        if (query == nullptr) {
            // Create a dummy sequence of the correct length
            QByteArray dummySeq(queryLen, 'N');
            query = new Query(queryName, dummySeq);
            queries.addQuery(query);
        }

        // Find the node in the graph
        DeBruijnNode *node = findNodeByName(targetName);
        if (node) {
            auto *hit = new Hit(query, node,
                                -1, alnLen,
                                -1, -1,
                                queryStart, queryEnd,
                                targetStart, targetEnd, 0, 0);
            query->addHit(hit);
            hitsImported++;
        }
    }

    return hitsImported;
}

int GraphSearchDialog::importBlastTabular(const QString &fullFileName) {
    QFile file(fullFileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return 0;

    Queries &queries = m_graphSearch->queries();
    int hitsImported = 0;

    QTextStream in(&file);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#'))
            continue;

        QStringList parts = line.split('\t');
        if (parts.size() < 12)
            continue;

        // BLAST tabular format (-outfmt 6):
        // 0: queryName, 1: subjectName, 2: identity, 3: alignLen
        // 4: mismatches, 5: gapOpens, 6: queryStart, 7: queryEnd
        // 8: subjectStart, 9: subjectEnd, 10: evalue, 11: bitScore
        QString queryName = parts[0];
        QString subjectName = parts[1];
        double identity = parts[2].toDouble();
        int alignLen = parts[3].toInt();
        int mismatches = parts[4].toInt();
        int gapOpens = parts[5].toInt();
        int queryStart = parts[6].toInt();
        int queryEnd = parts[7].toInt();
        int subjectStart = parts[8].toInt();
        int subjectEnd = parts[9].toInt();
        double evalue = parts[10].toDouble();
        double bitScore = parts[11].toDouble();

        // Get or create query
        Query *query = queries.getQueryFromName(queryName);
        if (query == nullptr) {
            // Create a dummy sequence with estimated length
            // We'll update it later if we find a longer hit
            query = new Query(queryName, QString());
            queries.addQuery(query);
        }

        // Update query length if this hit extends beyond current estimate
        int estimatedLen = query->getLength();
        if (queryEnd > estimatedLen) {
            // Resize the dummy sequence to accommodate the longest hit
            QString newSeq(queryEnd, 'N');
            query->setSequence(newSeq);
        }

        // Find the node in the graph
        DeBruijnNode *node = findNodeByName(subjectName);
        if (node) {
            auto *hit = new Hit(query, node,
                                identity, alignLen,
                                mismatches, gapOpens,
                                queryStart, queryEnd,
                                subjectStart, subjectEnd,
                                SciNot(evalue), bitScore);
            query->addHit(hit);
            hitsImported++;
        }
    }

    return hitsImported;
}

int GraphSearchDialog::importHmmerDomtbl(const QString &fullFileName) {
    QFile file(fullFileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return 0;

    Queries &queries = m_graphSearch->queries();
    int hitsImported = 0;

    QTextStream in(&file);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#'))
            continue;

        QStringList parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (parts.size() < 23)
            continue;

        // HMMER domtbl format:
        // 0: targetName, 1: targetAccession, 2: tlen
        // 3: queryName, 4: queryAccession, 5: qlen
        // 6: E-value, 7: score, 8: bias
        // 9: domain, 10: ndom, 11: cEvalue, 12: iEvalue, 13: dScore, 14: dBias
        // 15: hmmFrom, 16: hmmTo, 17: aliFrom, 18: aliTo
        // 19: envFrom, 20: envTo, 21: acc, 22: description
        QString targetName = parts[0];
        // int targetLen = parts[2].toInt(); // unused
        QString queryName = parts[3];
        int queryLen = parts[5].toInt();
        double evalue = parts[6].toDouble();
        double bitScore = parts[7].toDouble();
        int aliFrom = parts[17].toInt();
        int aliTo = parts[18].toInt();

        // Get or create query
        Query *query = queries.getQueryFromName(queryName);
        if (query == nullptr) {
            QByteArray dummySeq(queryLen, 'N');
            query = new Query(queryName, dummySeq);
            queries.addQuery(query);
        }

        // Find the node in the graph
        DeBruijnNode *node = findNodeByName(targetName);
        if (node) {
            auto *hit = new Hit(query, node,
                                -1, aliTo - aliFrom + 1,
                                -1, -1,
                                1, queryLen, // Full query coverage for HMM hits
                                aliFrom, aliTo,
                                SciNot(evalue), bitScore);
            query->addHit(hit);
            hitsImported++;
        }
    }

    return hitsImported;
}

void GraphSearchDialog::clearAllQueries() {
    clearHits();
    m_graphSearch->queries().clearAllQueries();
    updateTables();
    emit changed();
}

void GraphSearchDialog::clearSelectedQueries() {
    auto *selectionModel = ui->blastQueriesTable->selectionModel();
    if (!selectionModel->hasSelection())
        return;

    auto *proxyModel = qobject_cast<QSortFilterProxyModel *>(ui->blastQueriesTable->model());
    auto selectedRows = selectionModel->selectedRows();

    for (const auto &proxyIndex : selectedRows) {
        auto sourceIndex = proxyModel->mapToSource(proxyIndex);
        if (auto *query = m_queriesListModel->query(sourceIndex)) {
            query->clearSearchResults();
        }
    }

    updateTables();
    emit changed();
}

void GraphSearchDialog::openFiltersDialog() {
    HitFiltersDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
        setFilterText();
        updateTables();
        emit changed();
    }
}

// PathButtonDelegate implementation
void PathButtonDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const {
    const auto *model = qobject_cast<const QSortFilterProxyModel*>(index.model());
    auto *query = qobject_cast<const QueriesListModel*>(model->sourceModel())->query(model->mapToSource(index));
    if (query && query->wasSearchedFor()) {
        QStyleOptionButton btn;
        btn.features = QStyleOptionButton::None;
        btn.rect = option.rect;
        btn.state = option.state | QStyle::State_Enabled | QStyle::State_Raised;
        btn.text = QString::number(query->getPathCount());

        QStyle *style = option.widget ? option.widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_PushButton, &btn, painter);
        return;
    }

    QStyledItemDelegate::paint(painter, option, index);
}

bool PathButtonDelegate::editorEvent(QEvent *event, QAbstractItemModel *model, const QStyleOptionViewItem &option,
                                     const QModelIndex &index) {
    if (event->type() == QEvent::MouseButtonRelease) {
        const auto *proxyModel = qobject_cast<const QSortFilterProxyModel*>(model);
        auto *query = qobject_cast<const QueriesListModel*>(proxyModel->sourceModel())->query(proxyModel->mapToSource(index));
        if (query && query->wasSearchedFor()) {
            auto *queryPathsDialog = new QueryPathsDialog(query, nullptr);

            connect(queryPathsDialog,
                    &QueryPathsDialog::selectionChanged,
                    [this]() {
                        emit queryPathSelectionChanged();
                    });

            connect(queryPathsDialog, &QueryPathsDialog::finished,
                    queryPathsDialog, &QueryPathsDialog::deleteLater);

            queryPathsDialog->show();
        }
    }

    return QStyledItemDelegate::editorEvent(event, model, option, index);
}

// QueriesListModel implementation
QueriesListModel::QueriesListModel(Queries &queries, QObject *parent)
    : QAbstractTableModel(parent), m_queries(queries) {}

int QueriesListModel::rowCount(const QModelIndex &) const {
    return int(m_queries.get().getQueryCount());
}

int QueriesListModel::columnCount(const QModelIndex &) const {
    return int(QueriesHitColumns::TotalHitColumns);
}

QVariant QueriesListModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid())
        return QVariant();

    auto *query = m_queries.get().query(index.row());
    if (!query)
        return QVariant();

    auto column = QueriesHitColumns(index.column());

    if (role == Qt::DisplayRole) {
        switch (column) {
            case QueriesHitColumns::QueryName:
                return query->getName();
            case QueriesHitColumns::Type:
                return query->getSequenceType() == NUCLEOTIDE ? "Nucleotide" : "Protein";
            case QueriesHitColumns::Length:
                return QVariant::fromValue(qlonglong(query->getLength()));
            case QueriesHitColumns::Hits:
                return QVariant(int(query->getHits().size()));
            case QueriesHitColumns::QueryCover:
                return QString::number(query->fractionCoveredByHits() * 100.0, 'f', 1) + "%";
            case QueriesHitColumns::Paths:
                return query->getPaths().empty() ? "" : "View";
            default:
                return QVariant();
        }
    } else if (role == Qt::CheckStateRole && column == QueriesHitColumns::Show) {
        return query->isShown() ? Qt::Checked : Qt::Unchecked;
    } else if (role == Qt::DecorationRole && column == QueriesHitColumns::Color) {
        return query->getColour();
    }

    return QVariant();
}

QVariant QueriesListModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return QVariant();

    switch (QueriesHitColumns(section)) {
        case QueriesHitColumns::Color: return "Color";
        case QueriesHitColumns::Show: return "Show";
        case QueriesHitColumns::QueryName: return "Query name";
        case QueriesHitColumns::Type: return "Type";
        case QueriesHitColumns::Length: return "Length";
        case QueriesHitColumns::Hits: return "Hits";
        case QueriesHitColumns::QueryCover: return "Query cover";
        case QueriesHitColumns::Paths: return "Paths";
        default: return QVariant();
    }
}

Qt::ItemFlags QueriesListModel::flags(const QModelIndex &index) const {
    auto flags = QAbstractTableModel::flags(index);
    if (QueriesHitColumns(index.column()) == QueriesHitColumns::Show)
        flags |= Qt::ItemIsUserCheckable;
    return flags;
}

bool QueriesListModel::setData(const QModelIndex &index, const QVariant &value, int role) {
    if (!index.isValid())
        return false;

    if (role == Qt::CheckStateRole && QueriesHitColumns(index.column()) == QueriesHitColumns::Show) {
        if (auto *query = m_queries.get().query(index.row())) {
            query->setShown(value.toBool());
            emit dataChanged(index, index);
            return true;
        }
    }
    return false;
}

void QueriesListModel::setColor(const QModelIndex &index, QColor color) {
    if (!index.isValid())
        return;
    if (auto *query = m_queries.get().query(index.row())) {
        query->setColour(color);
        emit dataChanged(index, index);
    }
}

Query *QueriesListModel::query(const QModelIndex &index) const {
    if (!index.isValid())
        return nullptr;
    return m_queries.get().query(index.row());
}

// HitsListModel implementation
HitsListModel::HitsListModel(Queries &queries, QObject *parent)
    : QAbstractTableModel(parent) {
    update(queries);
}

int HitsListModel::rowCount(const QModelIndex &) const {
    return int(m_hits.size());
}

int HitsListModel::columnCount(const QModelIndex &) const {
    return int(HitsColumns::TotalHitColumns);
}

QVariant HitsListModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= int(m_hits.size()))
        return QVariant();

    auto *hit = m_hits[index.row()].hit;
    auto column = HitsColumns(index.column());

    if (role == Qt::DisplayRole) {
        switch (column) {
            case HitsColumns::QueryName:
                return hit->m_query->getName();
            case HitsColumns::NodeName:
                return hit->m_node->getName();
            case HitsColumns::PercentIdentity:
                if (hit->m_percentIdentity >= 0)
                    return QString::number(hit->m_percentIdentity, 'f', 1) + "%";
                return "";
            case HitsColumns::AlignmentLength:
                return QVariant(hit->m_alignmentLength);
            case HitsColumns::QueryCover:
                return QString::number(hit->getQueryCoverageFraction() * 100.0, 'f', 1) + "%";
            case HitsColumns::Mismatches:
                if (hit->m_numberMismatches >= 0)
                    return QVariant(hit->m_numberMismatches);
                return "";
            case HitsColumns::GapOpens:
                if (hit->m_numberGapOpens >= 0)
                    return QVariant(hit->m_numberGapOpens);
                return "";
            case HitsColumns::QueryStart:
                return QVariant(hit->m_queryStart);
            case HitsColumns::QueryEnd:
                return QVariant(hit->m_queryEnd);
            case HitsColumns::NodeStart:
                return QVariant(hit->m_nodeStart);
            case HitsColumns::NodeEnd:
                return QVariant(hit->m_nodeEnd);
            case HitsColumns::Evalue:
                if (hit->m_eValue.isZero())
                    return "";
                return hit->m_eValue.asString(false);
            case HitsColumns::BitScore:
                if (hit->m_bitScore >= 0)
                    return QString::number(hit->m_bitScore, 'f', 1);
                return "";
            default:
                return QVariant();
        }
    } else if (role == Qt::DecorationRole && column == HitsColumns::Color) {
        return m_hits[index.row()].query->getColour();
    }

    return QVariant();
}

QVariant HitsListModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return QVariant();

    switch (HitsColumns(section)) {
        case HitsColumns::Color: return "Color";
        case HitsColumns::QueryName: return "Query";
        case HitsColumns::NodeName: return "Node";
        case HitsColumns::PercentIdentity: return "Identity";
        case HitsColumns::AlignmentLength: return "Aln. len.";
        case HitsColumns::QueryCover: return "Query cover";
        case HitsColumns::Mismatches: return "Mism.";
        case HitsColumns::GapOpens: return "Gaps";
        case HitsColumns::QueryStart: return "Q. start";
        case HitsColumns::QueryEnd: return "Q. end";
        case HitsColumns::NodeStart: return "N. start";
        case HitsColumns::NodeEnd: return "N. end";
        case HitsColumns::Evalue: return "E-value";
        case HitsColumns::BitScore: return "Bit score";
        default: return QVariant();
    }
}

void HitsListModel::update(Queries &queries) {
    beginResetModel();
    m_hits.clear();
    for (auto *query : queries.queries()) {
        for (auto &hit : query->getHits()) {
            m_hits.push_back({query, hit.get()});
        }
    }
    endResetModel();
}
