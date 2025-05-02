# Cxy Documentation

> Cxy programming language is a work in progress, feel free to play around
> and report any issues. :construction:

## Comments

`cxy` supports `c`-style comments

* `// comment` for single line comments
* `/* comment */` for multi-line comments, nesting supported.

## Literals

### Null Literal

`null` literal has different use cases;

* used to nullify pointer value.
* when used with optional values, the value will be invalidated.
* when assigned to class, struct, tuple or union values reference drop or destructor
  will be invoked on the target variable.

### Boolean Literals

`true`, `false`

### Characters

By default, character literals are of type `wchar` which is a wide char. A char literal
can be bound to a desired type using the `:<type>` operator.

```c
'a'
'\n' // escaped character
'☺️' // wide character
'! ' as char // bound to type char
```

### Integers

The default type for integer literals is evaluated based on the smallest type that can
fit the integer literal. If a specific type is desired, the literal can be bound to the type.

```c
1    // decimal
0b01 // binary
0o77 // octal
0xaf // hex

100 as i64 // Bound to type i64
```

### Float Literals

The default type for float literals is `f64`, if `f32` is desired, the literal can be bound
to the type.

```c
0.00    // float
1e10    // exponential
1e-10   // exponential
1.32 as f32  // bound to f32
```

### String Literals

String literals are null terminated `char` (or `i8`) arrays with type `string`.

* nullable
* enumerable
* indexable
* supported by the `len` macro

```c
var s = "Hello World";
s.[0]       // indexable
s = null    // nullable
for (const x: s) {} // enumerable
len!(s)
```

### Variables

Variables can be declared with the `var` (mutable) or `const` keyword (immutable).

* Global variables are supported
* Initializer for `const` variables is required
* Variable type is optional, if not specified, it will be inferred from
    * initializer if provided
    * first assignment (variable cannot be used before assignment)
* Multiple variable declarations supported

```c
var a = 10; // type of variable inferred from the initializer
const b: f32 = 5.7; // variable type specified
var x;
// later
x = 10;
const a, _, b = get(); // multiple variable declaration needs a tuple initializer
const a, _ = (true, "Hello"); // same const a = true;
```

## String Expressions

`cxy` supports string interpolation on primitive types, tuples, unions and custom types that
implement the `str` operator

```c
var a,b = (10, 20);
println(f"${a} + ${b} = ${a + b}") // prints 10 + 20 = 30

struct Hello {
    const func `str`(os: OutputStream) { os << "\"Hello World\"" }
}
var msg = ("Jane", Hello{});
println(f"Greeting: ${msg}")   // prints Greeting: (Jane, "Hello World")
```

## Types

### Primitive Types

The following primitive types are supported by cxy;

```c
// Boolean Type
bool

// Char Types
char        // 1 byte char
wchar       // Wide character, 4 bytes

// Signed Integer Types
i8, i16, i32, i64

// Unsigned Integer Types
u8, u16, u32, u64

// Floating point types
f32, f64

// String type (null terminated string)
string
```

### Compound Types

### Arrays

Arrays are similar to `c` arrays, denoted as `[Type, Size]` (e.g., `[i32, 10]` is an array
of 10 integer types).

* Arrays are enumerable and indexable
* They support the `len!` macro which will return the number of elements in the array

```c
var a: [i32, 3];
// Indexing into an array
a.[0] = 1
a.[1] = 2
a.[3] = 3
// Enumerating an array
for (const x: a) {
    println(x)
}
```

### Slice

Slice types are unsized arrays denoted by the syntax `[Type]`, where the size is omitted.

* Enumerable and indexable
* Can be created from array types or via the `Slice` constructor
* Internal represented by the builtin `Slice` type

```c
func show(nums: [i32]) {
}

// Uses array literals (discussed in expressions section) to initialize an array
var a:[i32, 3] = [ 0 as i32, 1, 2] ;
// Compiler will create a slice of the array
show(a)

// Create slice using the `Slice` constructor
var msgs: ^Messages = alloc[Message](10);
var slice = Slice[Messages](msgs, 10);
```

### Tuples

`cxy` supports tuple types which groups multiple types into a single type. A tuple is like
an array type with each index having a different type.

* Tuples supports integral member expressions
* Values can be unpacked using the `...` operator

```c
var a: (i32, string) = (10, "Hello");
var b = a.1; // a holds the string "Hello"
a.0 == 10; // true
```

### Pointers

A pointer type is denoted by the `^` before the type name. Pointer type variable stores the
memory addresses other variables.

* the `ptrof` operator can be used to get the pointer of a variable
* just like in `c`, a `null` literal can be assigned to a pointer
* Pointer addition arithmetic allowed via the `ptroff!` macro
* Pointers can be re-type, which is the same as casting but with more relaxed rules
* Pointers are indexable with the `.[]` operator
* Arrays and strings are considered pointer types

```c
var x = 100 as u32;
// getting the address of a variable
var y: ^u32 = ptrof x;
var s = "hello";
// retype a string to a pointer type
var p = s !: ^char;
// perform pointer arithmetic (note that the ptroff macro is required to do offsets)
var c = ptroff!(s + 2);
// perform a pointer dereference (equivalent to c.[0])
var d = *c; 
```

### References

References are just like C pointers but only useful for structural types such as struct
classes, tuples, and unions.

* A reference type is denoted by the `&` character before the name of a type.
* Reference type variable work just like normal types, the compiler will take care of
  dereferencing where necessary.
* The `&` operator can be used to get a reference to a variable.

```c
func hello(msg: &Message) { }
var x = Message("Hello");
// Take a reference to variable x
hello(&x)
```

### Optionals

A type can be made optional by adding the `?` character at the end of the type name.

* `null` can be assigned to an optional variable, marking is not having a value
* builtin function `Some` can be used to create an optional value, `None` can be used
  to create an invalid optional value.
* Optional implements the truthy `!!` operator which returns true of the optional has a valid
  value.
* Implements the dereference operator `*` which returns the underlying optional value.

```c
// Creates an invalid optional i32 value
var x:i32? = null;
// Same as above
var y = None[i32]();
// Creates a valid i32 value
var y:i32? = 10;
// Same as above
var y = Some(10);

// Test the optional for validity
if (y) {
    // Get underlying optional value
    var z = *y;
}
```

### Tagged Union (Or Sum Types)

A tagged union is a list of types seperated by the `|` character that a variable can assume.

* They are called tagged because the compiler will generate a tag to go with the union type.
* A tagged union variable can be cast to a concrete type, provided they type is a union member
* `match` statement can be used to execute a certain block of code depending on the type of
  value stored on the union variable.

```c
var a: i32|string = 10 as i32;
a = "Hello World";

var b = <string>a; // Ok since string is in the union i32|string

var c = <i32>a; // Ok at compile time, but will crash at run-time since a has a string value

// Use match statement to deal with union values (prints "Hello World")
match(a) {
    case string as e => println(e)
    case i32 as e => println("We have ", e)
}

```

### Function Types

`cxy` supports function types which can with the following syntax

```c
// greeter should be a function that accepts a string argument and returns nothing
func say(msg: string, greeter: func(name: string) -> void) {
}

// Alias to a function that take a string as an argument and returns a string
type Greeter = func(name: string) -> string
```

### Aliased Types

`cxy` supports type aliasing which allows referring to a type by a different name or
compounding naming compound types such as unions, references, optional, pointers or
function types.

* Alias types accept generic parameters

```c
type Int32 = i32
type Unsigned = u8 | u16 | u32 | u64
type Handler = func(req: http.Request, http.Respose) -> void

// Generic parameters
type Result[T] = Error | T

```

## Functions

`cxy` supports functions and external function declarations

* Functions are private to the module by default, there can be made global by prefixing
  the declaration with `pub` keyword.
* The return type of function is optional (deduced by compiler if not specified). If the
  function is called declaration or is recursive, its return type will be required.
* Simple function types can return expressions using the `=>` operator
* `@inline` attribute can be specified to force the function to be inlined
* Function type parameters allowed on functions, by default they are transformed to closure
  style function arguments. The attribute `@pure` can be used to keep the function type arguments
  c-style.

```c
// Example function decl
func name(arg0: Type, arg1: Type) : ReturnType {
}

// A globally available function declaration, note that return type is not speicified.
pub func hello() { }

// Return type is required since function is recursive
pub func enter(x: u32) : u32 {
    if (x == 0)
        return getCode()
    else
        return 1 + enter(x-1)
}

// - Return type needed since the function is called above before declaration
// - simple functions can return expression using the `=>` token
// - @inline attribute used to request that the function be inlined
@inline
pub func getCode(): u32 => 100
    
// Function takes function as an argument, by default only closure's would be allowed
func greet(fn: func(ms: string) -> void) {
}
// Invokable with closure
greet((ms: string) => { println(ms) })

// Adding `@pure` removes the closure requirement, meaning the function can actually
// be used with native functions
@[inline, pure]
func greet(fn: func(ms: string) -> void) {
}
```

### External Function Declaration

* Prefixed with the `extern` keyword.
* Extern functions do not have body since they are expected to be declared somewhere else (likely in a c source file
  or library)
* The function will need to be provided at link time, this can be as simply as linking against a
  library that provides one or authoring one on `c`.

```c
pub extern func strlen(s: string) : u64
pub extern func sprintf(s: ^char, fmt: string, ...args: auto) : i64;
```