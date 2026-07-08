// Copyright 2024 Pathfinder contributors

// This file is part of Pathfinder

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

#pragma once

#include "io/bed.h"

#include <QDialog>
#include <QAbstractTableModel>
#include <QStyledItemDelegate>
#include <vector>

namespace Ui {
class BedDialog;
}

struct BedEntry {
    bed::Line line;
    QColor color;
    bool visible = true;
    QString nodeName;  // Resolved node name for display
    QString sourceFile;  // Source file this entry came from
};

class BedTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit BedTableModel(std::vector<BedEntry> &entries, QObject *parent = nullptr);

    int rowCount(const QModelIndex &) const override;
    int columnCount(const QModelIndex &) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;

    void update() { beginResetModel(); endResetModel(); }

    std::reference_wrapper<std::vector<BedEntry>> m_entries;
};

class BedDialog : public QDialog {
    Q_OBJECT
public:
    explicit BedDialog(QWidget *parent = nullptr);
    ~BedDialog() override;

    const std::vector<BedEntry> &entries() const { return m_entries; }

private:
    Ui::BedDialog *ui;
    std::vector<BedEntry> m_entries;
    BedTableModel *m_tableModel;
    QStringList m_loadedFiles;  // Track loaded files

    void loadBedFile(const QString &fullFileName);
    void applyAnnotations();
    void updateAnnotations();
    void updateSourceFileFilter();

private slots:
    void loadFileButtonClicked();
    void sourceFileChanged(int index);
    void deleteSelectedEntries();
    void removeSourceFile();
    void addNewRow();
};
