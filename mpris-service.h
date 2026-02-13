
#ifndef MPRIS_SERVICE_H
#define MPRIS_SERVICE_H

#include "mpris-interface.h"

extern MediaPlayer2 *mprisPlayerSkeleton;
extern MediaPlayer2Player *mprisPlayerPlayerSkeleton;

int start_mpris_service();
void stop_mpris_service();

#endif /* #ifndef MPRIS_SERVICE_H */
