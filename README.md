## `cxy`

**C-xy** (pronounced sexy) is a typed transpiled programming language
that is transpiled to `C`. `cxy` started as a compile design learning
project and personal hobby. The language references code from different
languages in the www

```c
import { println } from "stdlib/io.cxy"

func main(args: const [string]) {
    for (const arg: args) {
        println(arg)
    }
}
```

### Language features

`cxy` supports most features that would be supported on most
object-oriented programming languages (see the `tests/lang` directory
for some of the supported features)

#### Building

Requires `cmake` (any version greater that `3.16`) and `c` compiler. The following commands
should build the compiler binary `cxy` and an `stdlib` folder

```bash
mkdir build-dir
cd build-dir
cmake ..
make

```

