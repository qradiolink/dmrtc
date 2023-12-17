// Written by Adrian Musceac YO8RZZ , started March 2016.
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

#include "logger.h"


#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
void logMessage(QtMsgType type, const QMessageLogContext &context,
                const QString &msg)
{
    Q_UNUSED(context);
#else
void logMessage(QtMsgType type, const char *msg)
{
#endif
    QString time= QDateTime::currentDateTime().toString(
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

Logger::Logger(QObject *parent)
{
    Q_UNUSED(parent);
    QDir files = QDir::homePath();
    if(!QDir(files.absolutePath()+"/.config/dmrtc").exists())
    {
        QDir().mkdir(files.absolutePath()+"/.config/dmrtc");
    }
    QFileInfo log_file = files.filePath(".config/dmrtc/dmrtc.log");
    if(!log_file.exists())
    {
        QString txt = "[Log start]\n";
        QFile newfile(log_file.absoluteFilePath());

        if (newfile.open(QIODevice::WriteOnly | QIODevice::Append))
        {
            newfile.write(txt.toStdString().c_str());
            newfile.close();
        }
    }
    _log_file = new QFile(log_file.absoluteFilePath());
    _log_file->open(QIODevice::WriteOnly | QIODevice::Append);
    _stream = new QTextStream(_log_file);
    _console_log = false;
}

Logger::~Logger()
{
    delete _stream;
    _log_file->close();
    delete _log_file;
}

void Logger::set_console_log(bool value)
{
    _console_log = value;
}

void Logger::log(int type, QString msg)
{
    _mutex.lock();
    QString time= QDateTime::currentDateTime().toString(
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

    if(err)
        std::cerr << txt.toStdString() << std::endl;
    else
        std::cout << txt.toStdString() << std::endl;
    if(!_console_log)
    {
        emit applicationLog(txt);
    }

    *_stream << txt << Qt::endl;
    _mutex.unlock();

}


