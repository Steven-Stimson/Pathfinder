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

#include "viewstate.h"

#include <QDataStream>
#include <QIODevice>

QByteArray ViewState::serialize() const {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);

    // GFA data
    stream << gfaData;

    // View parameters
    stream << zoom << rotation;

    // Color scheme
    stream << static_cast<int>(colorScheme);

    // Per-node colors
    stream << static_cast<quint32>(nodeColors.size());
    for (const auto &[name, color] : nodeColors) {
        stream << QString::fromStdString(name) << color;
    }

    // Per-node cap styles
    stream << static_cast<quint32>(nodeCapStyles.size());
    for (const auto &[name, capStyle] : nodeCapStyles) {
        stream << QString::fromStdString(name) << static_cast<qint32>(capStyle);
    }

    // Per-node line points
    stream << static_cast<quint32>(nodeLinePoints.size());
    for (const auto &[name, points] : nodeLinePoints) {
        stream << QString::fromStdString(name);
        stream << static_cast<quint32>(points.size());
        for (const auto &pt : points) {
            stream << pt;
        }
    }

    // Selected nodes
    stream << static_cast<quint32>(selectedNodes.size());
    for (const auto &name : selectedNodes) {
        stream << QString::fromStdString(name);
    }

    return data;
}

ViewState ViewState::deserialize(const QByteArray &data) {
    ViewState state;
    QDataStream stream(data);

    // GFA data
    stream >> state.gfaData;

    // View parameters
    stream >> state.zoom >> state.rotation;

    // Color scheme
    int schemeInt;
    stream >> schemeInt;
    state.colorScheme = static_cast<NodeColorScheme>(schemeInt);

    // Per-node colors
    quint32 colorCount;
    stream >> colorCount;
    for (quint32 i = 0; i < colorCount; ++i) {
        QString name;
        QColor color;
        stream >> name >> color;
        state.nodeColors[name.toStdString()] = color;
    }

    // Per-node cap styles
    quint32 capStyleCount;
    stream >> capStyleCount;
    for (quint32 i = 0; i < capStyleCount; ++i) {
        QString name;
        qint32 capStyle;
        stream >> name >> capStyle;
        state.nodeCapStyles[name.toStdString()] = static_cast<int>(capStyle);
    }

    // Per-node line points
    quint32 linePointsCount;
    stream >> linePointsCount;
    for (quint32 i = 0; i < linePointsCount; ++i) {
        QString name;
        stream >> name;
        quint32 pointCount;
        stream >> pointCount;
        std::vector<QPointF> points;
        points.reserve(pointCount);
        for (quint32 j = 0; j < pointCount; ++j) {
            QPointF pt;
            stream >> pt;
            points.push_back(pt);
        }
        state.nodeLinePoints[name.toStdString()] = std::move(points);
    }

    // Selected nodes
    quint32 selectedCount;
    stream >> selectedCount;
    for (quint32 i = 0; i < selectedCount; ++i) {
        QString name;
        stream >> name;
        state.selectedNodes.insert(name.toStdString());
    }

    return state;
}
