#include "networksignalling.h"

NetworkSignalling::NetworkSignalling(const Settings *settings, Logger *logger, QObject *parent)
    : QObject{parent}
{
    _settings = settings;
    _logger = logger;
}

NetworkSignalling::~NetworkSignalling()
{

}
