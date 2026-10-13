
#ifndef DBUS_SERVICE_H
#define DBUS_SERVICE_H

#include "dbus-interface.h"
extern ShairportSync *shairportSyncSkeleton;

int start_dbus_service();
void stop_dbus_service();
int dbus_service_is_running();

#endif /* #ifndef DBUS_SERVICE_H */
