# JSON

`cxy` stdlib comes with an implementation for parsing JSON into `cxy` type and
serializing values/objects into JSON.

**Module**: `stdlib/json.cxy`

## Serializing

The module provides the function `toJSON` (signature below) which can be used to
serialize `cxy` data/values to JSON.

`func toJSON[T](os: &OutputStream, val: &const T) : void`

* All primitive types and builtin string types supported by default
* Characters are serialized to numbers, JSON does not have `char` data types
* Arrays, slices and tuples are serialized to JSON arrays
* Optionals are supported, `None` values are serialized as `null`.
* Struct values are serialized to JSON objects, currently all struct fields will
  be serialized.

```c
import { toJSON } from "stdlib/json.cxy"

struct User {
    name: String
    age: i32
    data: (bool, string)
}

func serialize() {
    var user = User{name: "User", age: 100, data: (true, "Hello World")};
    toJSON(stdout, &user)
    // prints {"name": "User", "age": 100, data: [true, "Hello World"]}
}
```

## Parsing JSON

JSON strings can be passed into `cxy` types by constructing a `JSONParser` and
using it to parse JSON strings.

`func parse[T](p: JSONParser): !T`

* All primitive types are supported **except** for `string`.
* Allocated strings (`String` type) are supported, this makes it easier to deal
  with memory management.
* Structs are supported if marked with `@[json]` attribute. All keys in the JSON
  object should exist in the target `struct` type as fields.
    * Struct must be default constructible
* Classes and structs can implement `func fromJSON(p: &JSONParser): !void` function
  to add custom parsing.
    * Type must be default constructible
* Optional types are supported, JSON `null` will be treated as `cxy` `None`.

```c
class Colour {
    r: f64
    g: f64
    b: f64
    
    @inline func `init`() {}
    
    // Custom JSON parser
    func fromJSON(p: &JSONParser): !void {
        // assume values serialized as [r, b, b]
        p.expectChar('[')
        r = p.expectFloat[f64]()
        p.expectChar(',')
        g = p.expectFloat[f64]()
        p.expectChar(',')
        b = p.expectFloat[f64]()
        p.expectChar(']')
    }
}

@[json]
struct User {
    age = 0:u32
    name: String = null
    eyes = Colour()
}

func main(): !void {
    var p = JSONParser(
        "{ \"name\": \"John\", \"age\": 92, \"eyes\": [0.0, 0.5, 0.1] }"
    )

    const user = parse[User](&p);
}
```
