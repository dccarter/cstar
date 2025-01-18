## `cxy`

**C-xy** (pronounced sexy) is a typed compiled programming language. `cxy` started as a compiler design learning
project and personal hobby project. The language references code from different languages in the www

```c
func main(args: [string]) {
    for (const arg, _: args) {
        println(arg)
    }
}
```

### Key Language features

`cxy` supports most features that would be supported on most
object-oriented programming languages (see the `tests/lang` and
`src/cxy/stdlib` directories for some of the supported features)

* Rich syntax that is easy to grasp.
* Object-oriented with support for user defined value types (structs and tuples) and
  reference types (classes).
* Interoperable with `c` programming language.
* Meta programming (generics and compile time evaluation).
* Reference counting memory management.
* Transpiles to readable `c` code.
* Comes with an LLVM backend.

#### Building

Requires `cmake` (any version greater that `3.16`), clang and LLVM. The following commands
should build the compiler binary `cxy` and an `stdlib` folder

```bash
mkdir build-dir
cd build-dir
cmake ..
make

```

