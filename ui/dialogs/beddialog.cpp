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
#include "ui/bandagegraphicsview.h"

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
    ThickStart = 8,
    ThickEnd = 9,
    ItemRgb = 10,
    BlockCount = 11,
    BlockSizes = 12,
    BlockStarts = 13,
    TotalColumns = BlockStarts + 1
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

    if (role == Qt::DisplayRole || role == Qt::EditRole) {
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
            case BedColumns::ThickStart:
                return entry.line.thickStart >= 0 ? QVariant(qlonglong(entry.line.thickStart)) : QVariant(".");
            case BedColumns::ThickEnd:
                return entry.line.thickEnd >= 0 ? QVariant(qlonglong(entry.line.thickEnd)) : QVariant(".");
            case BedColumns::ItemRgb:
                return QString("%1,%2,%3").arg(entry.line.itemRgb.r).arg(entry.line.itemRgb.g).arg(entry.line.itemRgb.b);
            case BedColumns::BlockCount:
                return QVariant(qlonglong(entry.line.blocks.size()));
            case BedColumns::BlockSizes:
            {
                QStringList sizes;
                for (const auto &block : entry.line.blocks)
                    sizes << QString::number(block.end - block.start);
                return sizes.join(",");
            }
            case BedColumns::BlockStarts:
            {
                QStringList starts;
                for (const auto &block : entry.line.blocks)
                    starts << QString::number(block.start - entry.line.chromStart);
                return starts.join(",");
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
        case BedColumns::ThickStart: return "ThickStart";
        case BedColumns::ThickEnd: return "ThickEnd";
        case BedColumns::ItemRgb: return "RGB";
        case BedColumns::BlockCount: return "Blocks";
        case BedColumns::BlockSizes: return "BlockSizes";
        case BedColumns::BlockStarts: return "BlockStarts";
        default: return QVariant();
    }
}

Qt::ItemFlags BedTableModel::flags(const QModelIndex &index) const {
    auto flags = QAbstractTableModel::flags(index);
    if (BedColumns(index.column()) == BedColumns::Show)
        flags |= Qt::ItemIsUserCheckable;
    // Enable editing for all data columns except Color and computed columns
    auto column = BedColumns(index.column());
    if (column != BedColumns::Color && column != BedColumns::Show &&
        column != BedColumns::BlockCount)  // BlockCount is computed from blocks
        flags |= Qt::ItemIsEditable;
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

    if (role == Qt::EditRole) {
        // If value is empty, don't change anything
        if (value.toString().trimmed().isEmpty())
            return false;

        auto &entry = m_entries.get()[index.row()];
        auto column = BedColumns(index.column());

        if (column == BedColumns::Chrom) {
            entry.line.chrom = value.toString().toStdString();
            entry.nodeName.clear();  // Clear nodeName so it will be re-resolved
            emit dataChanged(index, index);
            return true;
        } else if (column == BedColumns::Start) {
            bool ok;
            int val = value.toInt(&ok);
            if (ok && val >= 0) {
                entry.line.chromStart = val;
                emit dataChanged(index, index);
                return true;
            }
            return false;
        } else if (column == BedColumns::End) {
            bool ok;
            int val = value.toInt(&ok);
            if (ok && val >= 0) {
                entry.line.chromEnd = val;
                emit dataChanged(index, index);
                return true;
            }
            return false;
        } else if (column == BedColumns::Name) {
            entry.line.name = value.toString().toStdString();
            emit dataChanged(index, index);
            return true;
        } else if (column == BedColumns::Score) {
            bool ok;
            int val = value.toInt(&ok);
            if (ok) {
                entry.line.score = val;
                emit dataChanged(index, index);
                return true;
            }
            return false;
        } else if (column == BedColumns::Strand) {
            QString str = value.toString().trimmed();
            if (str == "+")
                entry.line.strand = bed::Strand::NORMAL;
            else if (str == "-")
                entry.line.strand = bed::Strand::REVERSE_COMPLEMENT;
            else
                entry.line.strand = bed::Strand::UNKNOWN;
            emit dataChanged(index, index);
            return true;
        } else if (column == BedColumns::ThickStart) {
            if (value.toString() == ".") {
                entry.line.thickStart = -1;
            } else {
                bool ok;
                int val = value.toInt(&ok);
                if (ok && val >= 0) {
                    entry.line.thickStart = val;
                } else {
                    return false;
                }
            }
            emit dataChanged(index, index);
            return true;
        } else if (column == BedColumns::ThickEnd) {
            if (value.toString() == ".") {
                entry.line.thickEnd = -1;
            } else {
                bool ok;
                int val = value.toInt(&ok);
                if (ok && val >= 0) {
                    entry.line.thickEnd = val;
                } else {
                    return false;
                }
            }
            emit dataChanged(index, index);
            return true;
        } else if (column == BedColumns::ItemRgb) {
            QStringList rgb = value.toString().split(",");
            if (rgb.size() == 3) {
                bool ok1, ok2, ok3;
                int r = rgb[0].toInt(&ok1);
                int g = rgb[1].toInt(&ok2);
                int b = rgb[2].toInt(&ok3);
                if (ok1 && ok2 && ok3 && r >= 0 && r <= 255 && g >= 0 && g <= 255 && b >= 0 && b <= 255) {
                    entry.line.itemRgb.r = r;
                    entry.line.itemRgb.g = g;
                    entry.line.itemRgb.b = b;
                    entry.color = entry.line.itemRgb.toQColor();
                    emit dataChanged(index, index);
                    return true;
                }
            }
            return false;
        } else if (column == BedColumns::BlockSizes) {
            // Parse comma-separated block sizes
            QStringList sizes = value.toString().split(",");
            if (sizes.size() == static_cast<int>(entry.line.blocks.size())) {
                bool allOk = true;
                for (int i = 0; i < sizes.size(); ++i) {
                    bool ok;
                    int size = sizes[i].toInt(&ok);
                    if (!ok || size <= 0) {
                        allOk = false;
                        break;
                    }
                    entry.line.blocks[i].end = entry.line.blocks[i].start + size;
                }
                if (allOk) {
                    emit dataChanged(index, index);
                    return true;
                }
            }
            return false;
        } else if (column == BedColumns::BlockStarts) {
            // Parse comma-separated block starts (relative to chromStart)
            QStringList starts = value.toString().split(",");
            if (starts.size() == static_cast<int>(entry.line.blocks.size())) {
                bool allOk = true;
                for (int i = 0; i < starts.size(); ++i) {
                    bool ok;
                    int start = starts[i].toInt(&ok);
                    if (!ok || start < 0) {
                        allOk = false;
                        break;
                    }
                    int blockSize = entry.line.blocks[i].end - entry.line.blocks[i].start;
                    entry.line.blocks[i].start = entry.line.chromStart + start;
                    entry.line.blocks[i].end = entry.line.blocks[i].start + blockSize;
                }
                if (allOk) {
                    emit dataChanged(index, index);
                    return true;
                }
            }
            return false;
        }
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
    ui->bedTableView->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);

    connect(ui->loadFileButton, SIGNAL(clicked()), this, SLOT(loadFileButtonClicked()));
    connect(ui->addRowButton, SIGNAL(clicked()), this, SLOT(addNewRow()));

    // Source file filter
    connect(ui->sourceFileComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &BedDialog::sourceFileChanged);
    connect(ui->removeSourceButton, &QPushButton::clicked,
            this, &BedDialog::removeSourceFile);

    // Update annotations when Show checkbox is toggled
    connect(m_tableModel, &BedTableModel::dataChanged,
            [this](const QModelIndex &, const QModelIndex &) {
                applyAnnotations();
            });

    // Select All and Invert Selection buttons
    connect(ui->selectAllButton, &QPushButton::clicked, [this]() {
        for (auto &entry : m_entries) {
            entry.visible = true;
        }
        m_tableModel->update();
        applyAnnotations();
    });

    connect(ui->invertSelectionButton, &QPushButton::clicked, [this]() {
        for (auto &entry : m_entries) {
            entry.visible = !entry.visible;
        }
        m_tableModel->update();
        applyAnnotations();
    });

    // Delete selected entries
    connect(ui->deleteSelectedButton, &QPushButton::clicked,
            this, &BedDialog::deleteSelectedEntries);

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
    QStringList fileNames = QFileDialog::getOpenFileNames(
        this,
        "Load BED file(s)",
        g_memory->rememberedPath,
        "BED files (*.bed);;All files (*)");

    if (fileNames.isEmpty())
        return;

    for (const QString &fullFileName : fileNames) {
        loadBedFile(fullFileName);
    }

    g_memory->rememberedPath = QFileInfo(fileNames.first()).absolutePath();
}

void BedDialog::loadBedFile(const QString &fullFileName) {
    try {
        auto bedLines = bed::load(fullFileName.toStdString());
        QString baseName = QFileInfo(fullFileName).fileName();

        // Add entries from this file (append, don't clear)
        for (const auto &bedLine : bedLines) {
            BedEntry entry;
            entry.line = bedLine;
            entry.color = bedLine.itemRgb.toQColor();
            entry.visible = true;
            entry.sourceFile = baseName;

            // Resolve node name
            auto nodeName = g_assemblyGraph->getNodeNameFromString(bedLine.chrom.c_str());
            entry.nodeName = nodeName;

            m_entries.push_back(entry);
        }

        // Track loaded file
        if (!m_loadedFiles.contains(baseName)) {
            m_loadedFiles.append(baseName);
            updateSourceFileFilter();
        }

        m_tableModel->update();

        // Update status label with total entries
        ui->fileStatusLabel->setText(
            QString("%1 entries from %2 files").arg(m_entries.size()).arg(m_loadedFiles.size()));

        g_memory->rememberedPath = QFileInfo(fullFileName).absolutePath();

        applyAnnotations();

    } catch (std::exception &err) {
        QMessageBox::warning(this, "Error loading BED file",
            "There was an error when attempting to load:\n"
            + fullFileName + ":\n" + err.what() + "\n\n"
            "Please verify that this file has the correct format.");
    }
}

void BedDialog::updateSourceFileFilter() {
    ui->sourceFileComboBox->blockSignals(true);
    ui->sourceFileComboBox->clear();
    ui->sourceFileComboBox->addItem("all");
    for (const QString &file : m_loadedFiles) {
        ui->sourceFileComboBox->addItem(file);
    }
    ui->sourceFileComboBox->blockSignals(false);
}

void BedDialog::sourceFileChanged(int index) {
    if (index == 0) {
        // "all" selected - show all entries
        for (auto &entry : m_entries) {
            entry.visible = true;
        }
    } else if (index > 0 && index <= m_loadedFiles.size()) {
        // Specific file selected
        QString selectedFile = m_loadedFiles.at(index - 1);
        for (auto &entry : m_entries) {
            entry.visible = (entry.sourceFile == selectedFile);
        }
    }
    m_tableModel->update();
    applyAnnotations();
}

void BedDialog::applyAnnotations() {
    // Use a fixed group name for all BED annotations
    static const QString groupName = "BED Annotations";

    g_annotationsManager->removeGroupByName(groupName);
    auto &annotationGroup = g_annotationsManager->createAnnotationGroup(groupName);

    for (auto &entry : m_entries) {
        if (!entry.visible)
            continue;

        // Resolve node name from chrom if not already resolved
        if (entry.nodeName.isEmpty()) {
            entry.nodeName = g_assemblyGraph->getNodeNameFromString(entry.line.chrom.c_str());
        }

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

    // Force graphics view to update
    if (g_graphicsView) {
        g_graphicsView->viewport()->update();
    }
}

void BedDialog::updateAnnotations() {
    applyAnnotations();
}

void BedDialog::deleteSelectedEntries() {
    auto *selectionModel = ui->bedTableView->selectionModel();
    if (!selectionModel->hasSelection()) {
        QMessageBox::information(this, "No Selection",
                                       "Please select entries to delete.");
        return;
    }

    auto *proxyModel = qobject_cast<QSortFilterProxyModel *>(ui->bedTableView->model());
    auto selectedRows = selectionModel->selectedRows();

    // Collect source indices to remove
    std::vector<size_t> indicesToRemove;
    for (const auto &proxyIndex : selectedRows) {
        auto sourceIndex = proxyModel->mapToSource(proxyIndex);
        indicesToRemove.push_back(sourceIndex.row());
    }

    // Sort in reverse order to remove from end to start
    std::sort(indicesToRemove.rbegin(), indicesToRemove.rend());

    // Remove entries
    for (size_t index : indicesToRemove) {
        if (index < m_entries.size()) {
            m_entries.erase(m_entries.begin() + index);
        }
    }

    m_tableModel->update();
    applyAnnotations();

    ui->fileStatusLabel->setText(
        QString("%1 entries remaining").arg(m_entries.size()));
}

void BedDialog::removeSourceFile() {
    int index = ui->sourceFileComboBox->currentIndex();

    // If "all" is selected, clear everything
    if (index == 0) {
        m_entries.clear();
        m_loadedFiles.clear();
        updateSourceFileFilter();
        m_tableModel->update();
        applyAnnotations();
        ui->fileStatusLabel->setText("All entries cleared");
        return;
    }

    if (index < 0 || index > m_loadedFiles.size()) {
        QMessageBox::information(this, "No Source Selected",
                                       "Please select a source file to remove.");
        return;
    }

    QString fileToRemove = m_loadedFiles.at(index - 1);

    // Remove all entries from this source file
    m_entries.erase(
        std::remove_if(m_entries.begin(), m_entries.end(),
            [&fileToRemove](const BedEntry &entry) {
                return entry.sourceFile == fileToRemove;
            }),
        m_entries.end());

    // Remove from loaded files list
    m_loadedFiles.removeAt(index - 1);
    updateSourceFileFilter();

    m_tableModel->update();
    applyAnnotations();

    ui->fileStatusLabel->setText(
        QString("Removed %1. %2 entries remaining.")
            .arg(fileToRemove)
            .arg(m_entries.size()));
}

void BedDialog::addNewRow() {
    BedEntry entry;
    entry.line.chrom = "node1";
    entry.line.chromStart = 0;
    entry.line.chromEnd = 1000;
    entry.line.name = "new_entry";
    entry.line.score = 0;
    entry.line.strand = bed::Strand::NORMAL;
    entry.line.thickStart = 0;
    entry.line.thickEnd = 1000;
    entry.line.itemRgb.r = rand() % 256;
    entry.line.itemRgb.g = rand() % 256;
    entry.line.itemRgb.b = rand() % 256;
    entry.color = entry.line.itemRgb.toQColor();
    entry.visible = true;
    entry.sourceFile = "manual";

    // Resolve node name from chrom
    entry.nodeName = g_assemblyGraph->getNodeNameFromString(entry.line.chrom.c_str());

    m_entries.push_back(entry);

    // Track the manual source file if not already tracked
    if (!m_loadedFiles.contains("manual")) {
        m_loadedFiles.append("manual");
        updateSourceFileFilter();
    }

    m_tableModel->update();
    applyAnnotations();

    ui->fileStatusLabel->setText(
        QString("%1 entries").arg(m_entries.size()));
}
