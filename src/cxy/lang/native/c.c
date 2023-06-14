#include <fcntl.h>

#define C__fcntl_GETFL(fd) fcntl((fd), F_GETFL)
#define C__fcntl_SETFL(fd, flags) fcntl((fd), F_SETFL, (flags))

