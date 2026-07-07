// Copyright 2024 Pathfinder contributors

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

#include "beddialog.h"
#include "ui_beddialog.h"

#include "io/bedloader.h"
#include "graph/annotationsmanager.h"
#include "graph/assemblygraph.h"
#include "graph/debruijnnode.h"

#include "program/globals.h"
#include "program/memory.h"
#include "program/settings.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QColorDialog>
#include <QMessageBox>
#include <QSortFilterProxyModel>

inline constexpr double BED_MAIN_WIDTH = 1;
inline constexpr double BED_THICK_WIDTH = 1.3;
inline constexpr double BED_BLOCK_WIDTH = 1.6;

enum class BedColumns : unsigned {
    Color = 0,
    Show = 1,
    Chrom = 2,
    Start = 3,
    End = 4,
    Name = 5,
    Score = 6,
    Strand = 7,
    TotalColumns = Strand + 1
};

// BedTableModel implementation
BedTableModel::BedTableModel(std::vector<BedEntry> &entries, QObject *parent)
    : QAbstractTableModel(parent), m_entries(entries) {}

int BedTableModel::rowCount(const QModelIndex &) const {
    return int(m_entries.get().size());
}

int BedTableModel::columnCount(const QModelIndex &) const {
    return int(BedColumns::TotalColumns);
}

QVariant BedTableModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= int(m_entries.get().size()))
        return QVariant();

    const auto &entry = m_entries.get()[index.row()];
    auto column = BedColumns(index.column());

    if (role == Qt::DisplayRole) {
        switch (column) {
            case BedColumns::Chrom:
                return QString::fromStdString(entry.line.chrom);
            case BedColumns::Start:
                return QVariant(qlonglong(entry.line.chromStart));
            case BedColumns::End:
                return QVariant(qlonglong(entry.line.chromEnd));
            case BedColumns::Name:
                return QString::fromStdString(entry.line.name);
            case BedColumns::Score:
                return QVariant(entry.line.score);
            case BedColumns::Strand:
                switch (entry.line.strand) {
                    case bed::Strand::NORMAL: return "+";
                    case bed::Strand::REVERSE_COMPLEMENT: return "-";
                    default: return ".";
                }
            default:
                return QVariant();
        }
    } else if (role == Qt::CheckStateRole && column == BedColumns::Show) {
        return entry.visible ? Qt::Checked : Qt::Unchecked;
    } else if (role == Qt::DecorationRole && column == BedColumns::Color) {
        return entry.color;
    } else if (role == Qt::BackgroundRole && column == BedColumns::Color) {
        return entry.color;
    }

    return QVariant();
}

QVariant BedTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return QVariant();

    switch (BedColumns(section)) {
        case BedColumns::Color: return "Color";
        case BedColumns::Show: return "Show";
        case BedColumns::Chrom: return "Chrom";
        case BedColumns::Start: return "Start";
        case BedColumns::End: return "End";
        case BedColumns::Name: return "Name";
        case BedColumns::Score: return "Score";
        case BedColumns::Strand: return "Strand";
        default: return QVariant();
    }
}

Qt::ItemFlags BedTableModel::flags(const QModelIndex &index) const {
    auto flags = QAbstractTableModel::flags(index);
    if (BedColumns(index.column()) == BedColumns::Show)
        flags |= Qt::ItemIsUserCheckable;
    return flags;
}

bool BedTableModel::setData(const QModelIndex &index, const QVariant &value, int role) {
    if (!index.isValid() || index.row() >= int(m_entries.get().size()))
        return false;

    if (role == Qt::CheckStateRole && BedColumns(index.column()) == BedColumns::Show) {
        m_entries.get()[index.row()].visible = value.toBool();
        emit dataChanged(index, index);
        return true;
    }
    return false;
}

// BedDialog implementation
BedDialog::BedDialog(QWidget *parent)
    : QDialog(parent), ui(new Ui::BedDialog) {
    ui->setupUi(this);

    setWindowFlags(windowFlags() | Qt::Tool);

    m_tableModel = new BedTableModel(m_entries, ui->bedTableView);
    auto *proxyModel = new QSortFilterProxyModel(ui->bedTableView);
    proxyModel->setSourceModel(m_tableModel);
    ui->bedTableView->setModel(proxyModel);
    ui->bedTableView->setSortingEnabled(true);

    connect(ui->loadFileButton, SIGNAL(clicked()), this, SLOT(loadFileButtonClicked()));
    connect(ui->closeButton, SIGNAL(clicked()), this, SLOT(accept()));

    // Handle color column click
    connect(ui->bedTableView,
            &QTableView::clicked,
            this,
            [this, proxyModel](const QModelIndex &proxyIndex) {
                if (!proxyIndex.isValid())
                    return;

                auto sourceIndex = proxyModel->mapToSource(proxyIndex);
                auto column = BedColumns(sourceIndex.column());
                if (column != BedColumns::Color)
                    return;

                if (sourceIndex.row() >= int(m_entries.size()))
                    return;

                auto &entry = m_entries[sourceIndex.row()];
                QColor chosenColour = QColorDialog::getColor(entry.color, this,
                    "BED entry color", QColorDialog::ShowAlphaChannel);
                if (!chosenColour.isValid())
                    return;

                entry.color = chosenColour;
                m_tableModel->update();
                updateAnnotations();
            });
}

BedDialog::~BedDialog() {
    delete ui;
}

void BedDialog::loadFileButtonClicked() {
    QString fullFileName = QFileDialog::getOpenFileName(
        this,
        "Load BED file",
        g_memory->rememberedPath,
        "BED files (*.bed);;All files (*)");

    if (fullFileName.isEmpty())
        return;

    loadBedFile(fullFileName);
}

void BedDialog::loadBedFile(const QString &fullFileName) {
    try {
        auto bedLines = bed::load(fullFileName.toStdString());

        m_entries.clear();
        for (const auto &bedLine : bedLines) {
            BedEntry entry;
            entry.line = bedLine;
            entry.color = bedLine.itemRgb.toQColor();
            entry.visible = true;

            // Resolve node name
            auto nodeName = g_assemblyGraph->getNodeNameFromString(bedLine.chrom.c_str());
            entry.nodeName = nodeName;

            m_entries.push_back(entry);
        }

        m_groupName = QFileInfo(fullFileName).fileName();
        m_tableModel->update();

        ui->fileStatusLabel->setText(
            QString("%1 (%2 entries)").arg(QFileInfo(fullFileName).fileName()).arg(m_entries.size()));

        g_memory->rememberedPath = QFileInfo(fullFileName).absolutePath();

        applyAnnotations();

    } catch (std::exception &err) {
        QMessageBox::warning(this, "Error loading BED file",
            "There was an error when attempting to load:\n"
            + fullFileName + ":\n" + err.what() + "\n\n"
            "Please verify that this file has the correct format.");
    }
}

void BedDialog::applyAnnotations() {
    if (m_groupName.isEmpty())
        return;

    g_annotationsManager->removeGroupByName(m_groupName);
    auto &annotationGroup = g_annotationsManager->createAnnotationGroup(m_groupName);

    for (const auto &entry : m_entries) {
        if (!entry.visible)
            continue;

        auto it = g_assemblyGraph->m_deBruijnGraphNodes.find(entry.nodeName.toStdString());
        if (it == g_assemblyGraph->m_deBruijnGraphNodes.end())
            continue;

        DeBruijnNode *node = it.value();
        if (entry.line.strand == bed::Strand::REVERSE_COMPLEMENT)
            node = node->getReverseComplement();

        auto &annotation = annotationGroup.annotationMap[node].emplace_back(
            std::make_unique<Annotation>(entry.line.chromStart + 1, entry.line.chromEnd,
                                         entry.line.name));
        annotation->addView(std::make_unique<SolidView>(BED_MAIN_WIDTH, entry.color));
        annotation->addView(std::make_unique<BedThickView>(BED_THICK_WIDTH, entry.color,
                                                            entry.line.thickStart, entry.line.thickEnd));
        annotation->addView(std::make_unique<BedBlockView>(BED_BLOCK_WIDTH, entry.color, entry.line.blocks));
    }
}

void BedDialog::updateAnnotations() {
    applyAnnotations();
}
