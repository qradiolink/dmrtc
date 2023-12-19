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

#include <QApplication>
#include <QCoreApplication>
#include <QThread>
#include <QDebug>
#include <QObject>
#include <QString>
#include <QVector>
#include <QList>
#include <QMetaType>
#include <QtGlobal>
#include <string>
#include <csignal>
#include "src/mainwindow.h"
#include "src/controller.h"
#include "src/udpclient.h"
#include "src/logger.h"
#include "MMDVM/DMRData.h"


void connectGuiSignals(MainWindow *w, Controller *controller);

class Station;

void signal_handler(int signal)
{
    std::cout << "Received signal " << signal << std::endl;
    QApplication::quit();
}

int main(int argc, char *argv[])
{
    qRegisterMetaType<CDMRData>("CDMRData");
    qRegisterMetaType<QVector<unsigned char>>("QVector<unsigned char>");
    qRegisterMetaType<QVector<LogicalChannel*>*>("QVector<LogicalChannel*>*");
    qRegisterMetaType<QList<unsigned int>*>("QList<unsigned int>*");
    QCoreApplication *a = new QCoreApplication(argc, argv);
    QStringList arguments = QCoreApplication::arguments();
    delete a;
    bool headless = false;

    Logger *logger = new Logger;
    if((arguments.length() > 1) && (arguments.indexOf("-h") != -1))
    {
        logger->set_console_log(true);
        headless = true;

    }
    QCoreApplication *app;
    if(headless)
    {
        app = new QCoreApplication(argc, argv);
    }
    else
    {
        app = new QApplication(argc, argv);
    }



    /// Init main logic
    ///
    logger->log(Logger::LogLevelInfo, "Starting dmrtc");
    Settings *settings = new Settings(logger);
    settings->readConfig();
    /// Start remote command listener
    if(headless)
    {
        settings->headless_mode = 1;
        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);
        std::signal(SIGHUP, signal_handler);
    }
    else
    {
        settings->headless_mode = 0;
    }


    Controller *controller = new Controller(settings, logger);
    QVector<LogicalChannel*> *logical_channels = controller->getLogicalChannels();
    MainWindow *w;
    if(!headless)
    {
        /// Init GUI
        ///
        w = new MainWindow(settings, logger);
        connectGuiSignals(w, controller);
        /// requires the slots to be set up
        w->setLogicalChannels(logical_channels);
        w->show();
        w->activateWindow();
        w->raise();
    }

    /// Init threads
    ///

    QThread *t1 = new QThread;
    t1->setObjectName("controller");
    controller->moveToThread(t1);
    QObject::connect(t1, SIGNAL(started()), controller, SLOT(run()));
    QObject::connect(controller, SIGNAL(finished()), t1, SLOT(quit()));
    QObject::connect(controller, SIGNAL(finished()), controller, SLOT(deleteLater()));
    QObject::connect(t1, SIGNAL(finished()), t1, SLOT(deleteLater()));
    QObject::connect(controller, SIGNAL(finished()), app, SLOT(quit()));
    t1->start();

    int ret = app->exec();

    /// Cleanup on exit
    ///
    if(!headless)
        delete w;
    if(headless)
    {

    }
    controller->stop();
    settings->saveConfig();
    delete settings;
    logger->log(Logger::LogLevelInfo, "Stopping dmrtc");
    delete logger;
    delete app;
    return ret;
}

void connectGuiSignals(MainWindow *w, Controller *controller)
{

    /// controller to GUI
    QObject::connect(controller, SIGNAL(updateLogicalChannels(QVector<LogicalChannel*>*)),
                     w, SLOT(setLogicalChannels(QVector<LogicalChannel*>*)));
    QObject::connect(controller, SIGNAL(updateRegisteredMSList(QList<uint>*)),
                     w, SLOT(updateRegisteredMSList(QList<uint>*)));
    QObject::connect(controller, SIGNAL(updateTalkgroupSubscriptionList(QSet<uint>*)),
                     w, SLOT(updateTalkgroupSubscriptionList(QSet<uint>*)));
    QObject::connect(controller, SIGNAL(updateCallLog(uint,uint,bool)),
                     w, SLOT(updateCallLog(uint,uint,bool)));
    QObject::connect(controller, SIGNAL(updateMessageLog(uint,uint,QString,bool)),
                     w, SLOT(updateMessageLog(uint,uint,QString,bool)));
    QObject::connect(controller, SIGNAL(updateRejectedCallsList(uint,uint,bool)),
                     w, SLOT(updateRejectedCallsList(uint,uint,bool)));
    QObject::connect(controller, SIGNAL(pingResponse(uint,uint)),
                     w, SLOT(displayPingResponse(uint,uint)));
    /// GUI to controller
    QObject::connect(w, SIGNAL(channelEnable(uint,bool)), controller, SLOT(setChannelEnabled(uint,bool)));
    QObject::connect(w, SIGNAL(registrationRequested()), controller, SLOT(requestMassRegistration()));
    QObject::connect(w, SIGNAL(sendShortMessage(QString,uint)), controller, SLOT(sendUDTShortMessage(QString,uint)));
    QObject::connect(w, SIGNAL(pingRadio(uint,bool)), controller, SLOT(pingRadio(uint,bool)));
    QObject::connect(w, SIGNAL(resetPing()), controller, SLOT(resetPing()));

}

