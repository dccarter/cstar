#include <sys/wait.h>
#include <stdlib.h>

Array547 _allTestCases = (Array547){(struct Tuple543){
    ._0 = "__bswap",
    ._1 = _test1I_E
  }, (struct Tuple543){
      ._0 = "min/max",
      ._1 = _test7I_E
    }, (struct Tuple543){
        ._0 = "String::(indexOf/rIndexOf)",
        ._1 = _test17I_E
      }, (struct Tuple543){
          ._0 = "Terminal style",
          ._1 = _test31I_E
        }};

#ifdef __CXY_TEST_VARIABLE
typedef struct { __typeof(__CXY_TEST_VARIABLE[0]) *data; uint64_t len; } __TestCases;

int main(int argc, char *argv[])
{
    return builtins_runTests(
        (SliceI_sE){.data = argv, .len = argc},
        "/Users/dccarter/projects/cxy/src/cxy/runtime/builtins.test.cxy",
        (SliceI_TsFZOptionalI_Tsu64u64_E___E){
            .data = allTestCases,
            .len = sizeof(allTestCases)/sizeof(allTestCases[0])
        }
     );
}
#endif