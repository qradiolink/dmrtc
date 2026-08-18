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

#ifndef LOGGER_H
#define LOGGER_H

#include <QString>
#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QDebug>
#include <QMutex>
#include <QFileInfo>
#include <QDateTime>
#include <iostream>
#include <cstdint>


#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
void logMessage(QtMsgType type, const QMessageLogContext& context, const QString& msg, QFile* log_file);
#else
void logMessage(QtMsgType type, const char* msg, QFile* log_file);
#endif


class Logger  : public QObject
{
    Q_OBJECT
public:
    enum {
        LogLevelDebug = 0,
        LogLevelInfo = 1,
        LogLevelWarning = 2,
        LogLevelCritical = 3,
        LogLevelFatal = 4
    };
    explicit Logger(QObject* parent = 0);
    ~Logger();
    void log(int type, QString msg);
    void set_console_log(bool value);
    void set_log_level(uint8_t level);

signals:
    void applicationLog(QString msg);

private:
    uint8_t m_level;
    QFile* m_log_file;
    bool m_console_log;
    QTextStream* m_stream;
    QMutex m_mutex;
};

#endif // LOGGER_H
