#include "add.h"
#include <stdio.h>

void sendCommand(union Command *command)
{
    printf("sending %d %g\n", command->create.id, command->create.value);
}