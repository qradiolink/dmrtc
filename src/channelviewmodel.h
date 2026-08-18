/*
 *   Copyright (C) 2023-2026 by Adrian Musceac YO8RZZ
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef CHANNELVIEWMODEL_H
#define CHANNELVIEWMODEL_H

#include <QObject>
#include <QDebug>
#include <QFont>
#include <QBrush>
#include <QIcon>
#include <QAbstractTableModel>

const unsigned int ROWS = 7;
const unsigned int COLS = 2;

namespace ChannelState
{
    enum ChannelState {
        ChannelFree = 1,
        ChannelDisabled = 2,
        ChannelBusy = 3,
        ChannelUnused = 4,
        ChannelControl = 5,
    };
}

class ChannelViewModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    explicit ChannelViewModel(QObject* parent = nullptr);
    ~ChannelViewModel();
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;
    bool setChecked(const QModelIndex& index, const QVariant& value, int role);
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    void setColor(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole);
    void setState(const QModelIndex& index, const QVariant& value, int role);

signals:
    void channelStateChange(int row, int col, bool state);

private:
    QString m_grid_data[ROWS][COLS];
    QString  m_colors[ROWS][COLS];
    unsigned int m_check_state[ROWS][COLS];
    int m_state[ROWS][COLS];

};

#endif // CHANNELVIEWMODEL_H
