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

#include "channelviewmodel.h"


ChannelViewModel::ChannelViewModel(QObject *parent): QAbstractTableModel(parent)
{
    for(unsigned int i = 0;i<ROWS;i++)
    {
        for(unsigned int j=0;j<COLS;j++)
        {
            _colors[i][j] = "#FFFFFF";
            _check_state[i][j] = Qt::Checked;
            _state[i][j] = ChannelState::ChannelUnused;
        }
    }
}

ChannelViewModel::~ChannelViewModel()
{

}

int ChannelViewModel::rowCount(const QModelIndex & /*parent*/) const
{
   return ROWS;
}

int ChannelViewModel::columnCount(const QModelIndex & /*parent*/) const
{
    return COLS;
}

QVariant ChannelViewModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role == Qt::DisplayRole && orientation == Qt::Horizontal) {
        switch (section)
        {
        case 0:
            return QString("Timelot 1");
        case 1:
            return QString("Timelot 2");
        }
    }
    if (role == Qt::DisplayRole && orientation == Qt::Vertical) {
        return QString("RF channel %1").arg(section + 1);
    }
    return QVariant();
}

QVariant ChannelViewModel::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index))
        return false;
    int row = index.row();
    int col = index.column();

    switch (role) {
    case Qt::DisplayRole:
    {
        if (row == 0 && col == 0)
        {
            return "Control channel";
        }
        switch(_state[row][col])
        {

        }

        QString text = QString("Usage: %1").arg(_grid_data[row][col]);
        return text;
    }
    case Qt::FontRole:
    {
        QFont boldFont;
        boldFont.setBold(true);
        if (row == 0 && col == 0)
        {
            return boldFont;
        }
        if(_colors[row][col] == "#004d99")
        {
            return boldFont;
        }
        break;
    }
    case Qt::BackgroundRole:
    {
        return QBrush(QColor(_colors[row][col]));
        break;
    }
    case Qt::ForegroundRole:
    {
        if(_colors[row][col] == "#004d99")
        {
            return QBrush(QColor("#FFFFFF"));
        }
        return QBrush(QColor("#000000"));
        break;
    }
    case Qt::DecorationRole:
    {
        if(_check_state[index.row()][index.column()] == Qt::Unchecked)
        {
            return QIcon(":/res/user-busy.png");
        }
        if(_state[index.row()][index.column()] == ChannelState::ChannelBusy)
        {
            return QIcon(":/res/call-start.png");
        }
        break;
    }
    case Qt::TextAlignmentRole:
            return int(Qt::AlignCenter | Qt::AlignVCenter);
        break;
    case Qt::CheckStateRole:
        if (row == 0 && col == 0)
            return Qt::Checked;
        if (row == 0 && col == 1)
            return _check_state[row][col];
        if (row > 0)
            return _check_state[row][col];
        break;
    }
    return QVariant();
}

bool ChannelViewModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!checkIndex(index))
        return false;
    int row = index.row();
    int col = index.column();
    if (role == Qt::EditRole)
    {
        _grid_data[row][col] = value.toString();
        emit dataChanged(this->index(0, 0), this->index(ROWS - 1, COLS -1));
        return true;
    }
    if (role == Qt::CheckStateRole)
    {
        _check_state[row][col] = value.toInt();
        emit dataChanged(this->index(0, 0), this->index(ROWS - 1, COLS -1));
        if(value.toInt() == Qt::Checked)
        {
            emit channelStateChange(row, col, true);
        }
        if(value.toInt() == Qt::Unchecked)
        {
            emit channelStateChange(row, col, false);
        }
        return true;
    }
    return false;
}

void ChannelViewModel::setColor(const QModelIndex &index, const QVariant &value, int role)
{
    (void)role;
    if (!checkIndex(index))
        return;
    _colors[index.row()][index.column()] = value.toString();
    emit dataChanged(this->index(0, 0), this->index(ROWS - 1, COLS -1));
}

void ChannelViewModel::setState(const QModelIndex &index, const QVariant &value, int role)
{
    (void)role;
    if (!checkIndex(index))
        return;
    _state[index.row()][index.column()] = value.toInt();
    emit dataChanged(this->index(0, 0), this->index(ROWS - 1, COLS -1));
}

Qt::ItemFlags ChannelViewModel::flags(const QModelIndex &index) const
{
    if (!checkIndex(index))
        return Qt::ItemIsEnabled;
    int row = index.row();
    int col = index.column();
    if(row == 0 && col == 0)
        return Qt::ItemIsEnabled;
    return Qt::ItemIsUserCheckable | Qt::ItemIsEnabled; // | QAbstractTableModel::flags(index);
}
