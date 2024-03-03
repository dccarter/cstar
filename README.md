## `cxy`

**C-xy** (pronounced sexy) is a typed compiled programming language. `cxy` started as a compile design learning
project and personal hobby. The language references code from different languages in the www

```c
func main(argc: i32, argv: &string) {
    for (const i: 0..argc) {
        println(argv.[i])
    }
}
```

### Language features

`cxy` supports most features that would be supported on most
object-oriented programming languages (see the `tests/lang` and
`src/cxy/stdlib` directories for some of the supported features)

#### Building

Requires `cmake` (any version greater that `3.16`) and LLVM. The following commands
should build the compiler binary `cxy` and an `stdlib` folder

```bash
mkdir build-dir
cd build-dir
cmake ..
make

```

