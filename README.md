## `cxy`

**C-xy** (pronounced sexy) is a typed transpiled programming language
`cxy` started as a compile design learning is transpiled to `C`
project and reference code from different languages in the www

```c
import { println } from "stdlib/io.cxy"

func main(args: [string]) {
    println("Hello Wold!")
}
```

### Language features

`cxy` supports most features that would be supported on most
object-oriented programming languages (see the `tests/lang` directory
for some of the supported features)

#### Tuples

```c
var tup = (true, 100)
assert!(tup.0 == true)
assert!(tup.1 == 100)

// Declare a tuple type
type User = (string, u32, (u32, string, string, string))

const user : User = ("name", 25, (100, "street", "city", "country"))
```

#### Structs

- [ ] Static members
- [x] Operator overloading
- [ ] Base classes (partially supported)
- [ ] Visibility (Supported by the parser, `-` used to mark members as private)
- [ ] Constructor field initializers
- [x] Const methods (like in C++, methods that can be invoked on a const object)

```c
// Plain old C struct
struct User {
    age: u32
    name: string
}

// With methods (maybe should be a class)
struct Printer {
   - tab: u32 = 0
   - buffer: StringBuilder
   
   // `new` operator used as the constructor
   func `new`(=tab: u32 = 0) {
       buffer = StringBuilder()
   }
   
   @inline
   func print(x: string) : void {
       buffer << x
   }
   
   @inline
   func print(x: u32|i32) : void {
       buffer << x
   }
}
```
