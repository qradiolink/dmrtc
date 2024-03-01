// Written by Adrian Musceac YO8RZZ , started October 2023.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License as
// published by the Free Software Foundation; either version 3 of the
// License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

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

enum ChannelState
{
    ChannelFree = 1,
    ChannelDisabled = 2,
    ChannelBusy = 3,
    ChannelUnused = 4,
    ChannelControl = 5,
};

class ChannelViewModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    explicit ChannelViewModel(QObject *parent = nullptr);
    ~ChannelViewModel();
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    bool setChecked(const QModelIndex &index, const QVariant &value, int role);
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    void setColor(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole);
    void setState(const QModelIndex &index, const QVariant &value, int role);

signals:
    void channelStateChange(int row, int col, bool state);

private:
    QString _grid_data[ROWS][COLS];
    QString  _colors[ROWS][COLS];
    unsigned int _check_state[ROWS][COLS];
    int _state[ROWS][COLS];

};

#endif // CHANNELVIEWMODEL_H
