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

#include "logger.h"


#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
void logMessage(QtMsgType type, const QMessageLogContext& context,
                const QString& msg)
{
    Q_UNUSED(context);
#else
void logMessage(QtMsgType type, const char* msg)
{
#endif
    QString time = QDateTime::currentDateTime().toString(
                       "d/MMM/yyyy hh:mm:ss");
    QString txt;

    switch (type) {
        case QtInfoMsg:
            txt = QString("[%1] Info: %2").arg(time).arg(msg);
            break;

        case QtDebugMsg:
            txt = QString("[%1] Debug: %2").arg(time).arg(msg);
            break;

        case QtWarningMsg:
            txt = QString("[%1] Warning: %2").arg(time).arg(msg);
            break;

        case QtCriticalMsg:
            txt = QString("[%1] Critical: %2").arg(time).arg(msg);
            break;

        case QtFatalMsg:
            txt = QString("[%1] Fatal: %2").arg(time).arg(msg);
            break;
    }

    QFile outFile("dmrtc.log");
    outFile.open(QIODevice::WriteOnly | QIODevice::Append);
    QTextStream ts(&outFile);
    ts << txt << Qt::endl;
    outFile.close();
}

#if 0
#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
qInstallMessageHandler(logMessage);
#else
qInstallMsgHandler(logMessage);
#endif
#endif

Logger::Logger(QObject* parent)
{
    Q_UNUSED(parent);
    QDir files = QDir::homePath();

    if (!QDir(files.absolutePath() + "/.config/dmrtc").exists()) {
        QDir().mkdir(files.absolutePath() + "/.config/dmrtc");
    }

    QFileInfo log_file = files.filePath(".config/dmrtc/dmrtc.log");

    if (!log_file.exists()) {
        QString txt = "[Log start]\n";
        QFile newfile(log_file.absoluteFilePath());

        if (newfile.open(QIODevice::WriteOnly | QIODevice::Append)) {
            newfile.write(txt.toStdString().c_str());
            newfile.close();
        }
    } else {
        QString time = QDateTime::currentDateTime().toString(".d-MMM-yyyy-hh:mm:ss");
        QFileInfo log_file_old = log_file.absoluteFilePath().append(time);
        QDir(log_file.absoluteFilePath()).rename(log_file.absoluteFilePath(), log_file_old.absoluteFilePath());
        log_file = files.filePath(".config/dmrtc/dmrtc.log");
        QString txt = "[Log start]\n";
        QFile newfile(log_file.absoluteFilePath());

        if (newfile.open(QIODevice::WriteOnly | QIODevice::Append)) {
            newfile.write(txt.toStdString().c_str());
            newfile.close();
        }
    }

    m_log_file = new QFile(log_file.absoluteFilePath());
    m_log_file->open(QIODevice::WriteOnly | QIODevice::Append);
    m_stream = new QTextStream(m_log_file);
    m_console_log = false;
    m_level = 0;
}

Logger::~Logger()
{
    delete m_stream;
    m_log_file->close();
    delete m_log_file;
}

void Logger::set_console_log(bool value)
{
    m_console_log = value;
}

void Logger::set_log_level(uint8_t level)
{
    m_level = level;
}

void Logger::log(int type, QString msg)
{
    if ((uint8_t)type < m_level)
        return;

    m_mutex.lock();
    QString time = QDateTime::currentDateTime().toString(
                       "d/MMM/yyyy hh:mm:ss.zzz");
    QString txt;
    bool err = false;

    switch (type) {
        case LogLevelInfo:
            txt = QString("[%1] [Info] %2").arg(time).arg(msg);
            break;

        case LogLevelDebug:
            txt = QString("[%1] [Debug] %2").arg(time).arg(msg);
            break;

        case LogLevelWarning:
            txt = QString("[%1] [Warning] %2").arg(time).arg(msg);
            break;

        case LogLevelCritical:
            txt = QString("[%1] [Critical] %2").arg(time).arg(msg);
            err = true;
            break;

        case LogLevelFatal:
            txt = QString("[%1] [Fatal] %2").arg(time).arg(msg);
            err = true;
            break;
    }

    if (err)
        std::cerr << txt.toStdString() << std::endl;
    else
        std::cout << txt.toStdString() << std::endl;

    if (!m_console_log) {
        emit applicationLog(txt);
    }

    *m_stream << txt << Qt::endl;
    m_mutex.unlock();

}


