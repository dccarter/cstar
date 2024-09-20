#include "add.h"
#include "errno.h"
#include "time.h"
#include <stdio.h>

void sendCommand(union Command *command)
{
    time_t printf("sending %d %g\n", command->create.id, command->create.value);
}
