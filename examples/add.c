#include "add.h"
#include "unistd.h"
#include <pwd.h>
#include <stdlib.h>

#ifdef __MAIN_RETURN_TYPE__
__MAIN_RETURN_TYPE__ main(int argc, char *argv[]) {
#ifdef __MAIN_PARAM_TYPE__
    __MAIN_PARAM_TYPE__ args = args = {{ .data = argv, .len = argc }};
#define __MAIN_ARGS__ args
#else
#define __MAIN_ARGS__
#endif

#ifdef __MAIN_RAISED_TYPE__
    __MAIN_RAISED_TYPE__ ex = _main(__MAIN_ARGS__);
    if (ex.tag == 1) {
         unhandledException(ex._1);
         unreachable();
    }
#ifdef __MAIN_RETURNS__
     __MAIN_RETURN_TYPE__ ret = ex._0
    __MAIN_RAISED_TYPE_DCTOR__(&ex);
#else
     __MAIN_RAISED_TYPE_DCTOR__(&ex);
#endif
#else
#ifdef __MAIN_RETURNS__
      return _main(__MAIN_ARGS__);
#else
      _main(__MAIN_ARGS__);
#endif
#endif
}
#endif
