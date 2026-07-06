// Copyright 2024

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

#include "nodecolorer.h"

#include <QByteArray>
#include <QColor>
#include <QPointF>
#include <QString>

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct ViewState {
    // Graph data (GFA snapshot)
    QByteArray gfaData;

    // View parameters
    double zoom = 1.0;
    double rotation = 0.0;

    // Color scheme that was active at snapshot time
    NodeColorScheme colorScheme = RANDOM_COLOURS;

    // Per-node colors at snapshot time (node name → color)
    std::unordered_map<std::string, QColor> nodeColors;

    // Per-node cap style at snapshot time (node name → cap style)
    std::unordered_map<std::string, int> nodeCapStyles;

    // Per-node line points (path control points) at snapshot time (node name → points)
    std::unordered_map<std::string, std::vector<QPointF>> nodeLinePoints;

    // Selected node names at snapshot time
    std::unordered_set<std::string> selectedNodes;

    // Serialize to QByteArray
    QByteArray serialize() const;

    // Deserialize from QByteArray
    static ViewState deserialize(const QByteArray &data);
};
