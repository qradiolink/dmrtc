#ifndef NETWORKSIGNALLING_H
#define NETWORKSIGNALLING_H

#include <QObject>
#include "settings.h"
#include "logger.h"

class NetworkSignalling : public QObject
{
    Q_OBJECT
public:
    explicit NetworkSignalling(const Settings *settings, Logger *logger, QObject *parent = nullptr);
    ~NetworkSignalling();

signals:

private:
    const Settings *_settings;
    Logger *_logger;

};

#endif // NETWORKSIGNALLING_H
