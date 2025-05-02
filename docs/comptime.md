# Comptime

`cxy` support compile time code evaluation backed by builtin macros and comptime AST properties. Compile time evaluation
is intertwined with type checking, so type information will be available on the expressions.

- Generic type parameters are inherently comptime declarations, they are evaluated at comptime.
- Comptime declarations/statements are prefixed with the `#` operator.
- Comptime expressions are enclosed in `#{ ... }` braces.
- Binary and unary operators are supported at comptime on boolean, char, integer, and float literals.

### Variables

- All comptime variable declarations must be prefixed with the `const` keyword (i.e. `var` is not allowed in comptime).

```c
// Comptime literal 1
#const num = 1;
#const T = typeof!(true);
```

- Comptime variables can be accessed and updated at comptime.

```c
#const num = 1;
...
// Increament number by 1.
#{num = num + 1}
```

### `If`

Comptime `if` (prefixed with `#`) can be used to conditionally execute code at compile time.

- The condition must evaluate to a boolean literal.
- `else` statements without `#` are allowed at comptime. There was no point in using `#` because if we are saying `else`
  to a comptime `if` then the else block should be comptime.
- If the condition evaluates to `true`, the if block will be evaluated, otherwise it will be discarded and if an `else`
  exists, it will be evaluated instead.

```c
...
#const T = #i32;
#if (T == #i32) {
  // Since T is i32, "type is #i32" will be printed
  info!("type is {t}", #T)
}

#if (sizeof!(T) == 3) {
  // Since size of T (aka i32) is 4, this block is discarded
  info("This is weird")
}
else #if (sizeof!(T) == 2) {
  // Since size of T (aka i32) is 4, this block is discarded
  info!("This is a short")
}
else {
  // This is the else block to the last if, this will be printed
  info!("Size of {t} is {u64}", #T, sizeof!(T))
}
```

### `For`

Comptime `for` (prefixed with `#`) can be used to loop over comptime collections at compile time.
`#for(const <var>: <collection>) <body>`

- The iterable collection of this statement must always be comptime enumerable. Supported nodes include literal ranges,
  string literal, array literals, and comptime lists.
- Unlike a runtime `for`, the body of the comptime loop is unrolled into current code context. This means that if the
  body generates runtime code, it will be sequentially inserted into the current code context.
- For loops can be nested

```c
#for(const i: 0..3) {
  println(#{i})
}
// Same as
println(0)
println(1)
println(2)

#const T = #(bool, i32, string)
#for(const i: 0..T.membersCount) {
  #const M = typeat!(T, i)
  info!("member at {u64} is {t}", i, #M)
}
// This will print the following at comptime
"members at 0 is #bool"
"members at 1 is #i32"
"members at 2 is #string"

// Since the body does not generate any code, nothing will added to the current
// runtime code context
```

### Tuple Type Transformations

`cxy` provides syntactic sugar to transform tuple types into other tuple types using the following syntax;
`T as M,i => transform(T, i), condition(T, i)?`

Given a tuple `T` enumerate its members into variable member`M` and index variable `i`:

- For each member/index (`M`, `i`) run transformation `transform` on `M` if `condition` is not given or the given one
  evaluates to true.
- Add the result to a new tuple type.

```
#const T = #(bool, string, String);
// Transform to reference #(&bool, &string, &String)
#const Refs = #`T as M,i => &M`;
// Filter string type #(string, String)
#const Strs = #`T as M,i => M, M.isString`
// Reverse #(String, string, bool)
#const Complex = #`T as M, i => typeat!(T, T.membersCount - i - 1)`
```

### Builtin Macros

The language has a limited set of builtin macros that are evaluated during type checking. Note that there are user
defined macros that are evaluated by the processor and discussed in the preprocessor page.

- **`assert!(cond)`**: Creates a runtime assertion
    - If the assertion fails, it will print out the location and abort the application
      ```c
      const a = false;
      assert!(a)
      ```
- **`mk_ast_list!()`**: Creates a compile type AST list node. The list can be built and expanded into an expression
- **`ast_list_add!(list, node)`**: Used to add an AST not to a list of nodes. This is useful when dynamically building
  an ast
  ```c
  #const list = mk_ast_list!();
  ast_list_add!(list, true)
  ast_list_add!(list, "Hello World")
  println(#{list})
  // same as println(true, "Hello World")
  ```
- **`mk_bc!(Id, ...args)`**: Expands to a backend call expression associated with the given `Id`. `args` is dependent
  on the backend call being invoked. :warning: Only invoke backend calls when you know what you are doing. Supported
  id's:
    - `bfiSizeOf!`: expects one expression argument, get the size of the given argument
    - `bfiAlloca!`: expects one type argument, reserves memory on the stack for the given type
    - `bfiZeromem`: expects one r-value argument, resets the memory at the given r-value to all zero's
    - `bfiCopy`: expects one expression argument, copies the given expression using copy rules
- **`mk_field!(name, T, def?)`**: Expands to a struct field declaration named `name` of type `T` with an optional
  default
  value `def`. Useful when creating structs/classes at compile time.
- **`mk_field_expr!(name, value)`**: Expands to a field expression named `name` with value `value`.
- **`mk_ident!(name, ...parts)`**: Expands to an identifier whose value is a concatenation of `name` and the arguments
  passed as `parts`.
- **`mk_integer!(num)`**: Expands to an integer literal
- **`mk_str!(s, ...parts)`**: Expands to string literal that is a concatenation of `s` and arguments given in parts.
  ```c
  #const age = 105;
  #const name = "Turtle";
  var s = mk_str!("Hello ", name, ", you are ", age, " years old")
  // same as var s = "Hello Turtle, you are 105 years old"
  ```
- **`mk_struct_expr!(fields)`**: Expands to a struct expression with the given `fields`.
  ```c
  #const list = mk_ast_list!();
  ast_list_add!(list, mk_field_expr!(:name, "Turtle"))
  ast_list_add!(list, mk_field_expr!(:age, 105 as i32))
  var it = mk_struct_expr!(list)
  // same as var it = { name: "Turtle", age: 105 as i32 }
  ```
- **`mk_tuple_expr!(members)`**: Expands to a tuple expression with the given members

- **`base_of!(T)`**: Gets the base of the given type if any. If the type does not have
  a base, compilation will fail. Note that `T` must be a typeinfo node.
    ```c
    var car: base_of!(#Mazda) = ...
    ```
- **`column!`**: Expands to the current column number in the source file
- **`line!`**: Expands to the current line number in the source file
- **`file!`**: Expands to the current file path being compiled

- **`cstr!(str)`**: Converts the type of the given string literal to a c-style string type.
  i.e., changes the type of str (which **MUST** be `string` to `^const char`)
- **`error!(fmt, ...args)`**: Report an error message at compile time leading to compilation failure.
    - `fmt` argument must be string literal with static substitution placeholders (See static substitution)
    - `args` must be a list of arguments to be substituted onto the string
      ```c
      error!("my type is {t}", typeof!(arg))
      ```
- **`warn!(fmt, ...args)**: Reports a warning message at compile time. If warnings are treated as errors, this will
  fail compilation.
    - `fmt` argument must be string literal with static substitution placeholders (See static substitution)
    - `args` must be a list of arguments to be substituted onto the string
- **`info!(fmt, ...args)**: Reports informational messages at compile time.
    - `fmt` argument must be string literal with static substitution placeholders (See static substitution)
    - `args` must be a list of arguments to be substituted onto the string.
- **`ex!`**: This macro is available within exception catch blocks. It expands to the caught exception
    ```c
    fail() catch {
        stdout << ex!
    }
    ```

- **`has_member!(T, name, M)`**: Expands to `true` if type `T` has a member named `named` of type `M` and `false`
  otherwise. This works for structural types (structs and classes) and modules.
    ```c
    struct Cat {
        func sound() => "meow"
    }
    ...
    #const sounder = has_member!(#Cat, "sound", #func() -> string)
    ```

- **`has_type!(T, name)`**: Expands to `true` if type `T` has a type alias named `name` and false otherwise.
  This works for structural types (structs and classes) and modules.
    ```c
    struct Cat {
        type Id = i64;
    }
    var x = has_type!(#Cat, :Id)
    ```

- **`is_base_of!(Y, T)`**: Expands to `true` if type `Y` is a base of type `T`, `false` otherwise.
- **`indexof!(T, M)`**: Expands to an integer representing the index of type `M` within type `T`. Type `T` must be
  a tuple.
- **`lambda_of!(F)`**: Expands to a lambda type associated with the give function type `F`. This is useful when
  declaring closures as members in compound types.
  ```c
  type F = func(x: i32) -> i32
  struct Hello {
    fn: lambda_of!(F)
    func `init`(fn: F) {
      this.fn = &&fn
    }
  }
  ```
- **`sizeof!(X)`**: Expands to the size of the given type `X` (or the type of the express `X` if `X` is an expression)
- **`typeof!(x)`**: Expands to the type of the given expression `x`
- **`len!(x)`**: Expands to an expression that evaluates the length of the given expression `x`.
    - If `x` is a string literal, then this expands to `strlen(x)`
    - If `x` is an array, then this returns the number of elements in the array
    - If `x` is a struct, then this expands to `x.len` (type of `x` must have an integer member named `len` )
- **`typeat!(T, i)`**: Expands to the type at the given index `i` in type `T`. Type `T` must be a tuple type

### AST Properties

AST props makes it easier to compose expressions at compile time. These props are built-in and non-extensible. Note that
all props expand to comptime constants.

- **`.name`**: Gets the name of the target entity. Applicable to declarations, primitive types, annotations, attributes,
  and generic parameters.
- **`.value`**: Gets value of the target entity. Application to annotations
- **`.members`**: Gets a comptime list of members of the target entity. Applicable to class, struct, tuple, enum and
  union declaration nodes.
- **`.attributes`**: Gets attributes of the target AST node.
- **`.annotations`**: Gets annotations associated with a `class` or `struct`. This will be a comptime iterable list.
- **`.Tinfo`**: Gets type information associated with the give AST node. Currently applicable to
    - function params
    - class/struct field members
  ```c
  #for(const member: #{User.members}) {
    #const M = member.Tinfo;
  }
  ```
- **`.elementType`**: Get the element type of the target array
- **`.pointedType`**: Get the pointed type in the target pointer
- **`.strippedType`**: Get the stripped type in the target type. (pointed or referred type)
- **`.targetType`**: Get the target type for a given optional or result type.
- **`.callable`**: Get the callable component of the target type. Applicable to functions and lambdas, useful when used
  with lambdas.
  ```c
  type X = lambda_of!(#func(x: i32) -> void)
  // this creates a type (^void, func(_: ^void, x: i32) -> void)
  type Z = #{X.callable}
  // This resolves to type Z = func(x: i32) -> void
  ```
- **`.returnType`**: Get the return type of the target function. Applicable to functions or lambdas
- **`.baseType`**: Get the base type of the target type. Target must have a class Type
- **`.params`**: Get a list of parameters associated with target node. Applicable to attributes, functions, function
  types and lambda's
- **`.membersCount`**: Get the number of members on the target type. Applicable to unions, tuples, structs and classes
- **`.isInteger`**: Expands to true if the given node type is an integer type.
- **`.isSigned`**: Expands to true if the given node's type is a signed integer type.
- **`.isUnsigned`**: Expands to true if the given node's type is an unsigned integer type.
- **`.isFloat`**: Expands to true if the given node's type is a floating point type.
- **`.isOptional`**: Expands to true if the given node's type is an optional type.
- **`.isNumber`**: Expands to true if the given node's type a numeric type, i.e., an integer or float.
- **`.isPointer`**: Expands to true if the given node's type is a pointer type.
- **`.isReference`**: Expands to true if the given node's type is a reference type.
- **`.isUnresolved`**: Expands to true if the given node's type is unresolved.
- **`.isStruct`**: Expands to true if the given node's type is a `struct` type.
- **`.isClass`**: Expands to true if the given node's type is a `class` type.
- **`.isTuple`**: Expands to true if the given node's type is a tuple type.
- **`.isUnion`**: Expands to true if the given node's type is a union type.
- **`.isField`**: Expands to true if the given node is a struct/class field.
- **`.isBoolean`**: Expands to true if the given node's type is a boolean type.
- **`.isChar`**: Expands to true if the given node's type is `char` or `wchar`.
- **`.isArray`**: Expands to true if the given node's type is an array type.
- **`.isSlice`**: Expands to true if the given node's type is a slice type.
- **`.isEnum`**: Expands to true if the given node's type is an `enum` type.
- **`.isVoid`**: Expands to true if the given node's type is `void`.
- **`.isDestructible`**: Expands to true if the type of the given node is destructible (i.e., has a destructor).
- **`.isSigned`**: Expands to true if the given node's type is an unsigned integer type.
- **`.isFunction`**: Expands to true if the given node is a function decl or the type of the node is a function type.
- **`.isClosure`**: Expands to true if the given node is a closure, or the type of the node is a closure.
- **`.isFuncTypeParam`**: Expands to true if the given node is a function type parameter.
- **`.isSigned`**: Expands to true if the given node's type is an unsigned integer type.
- **`.isAnonymousStruct`**: Expands to true if the given node is an anonymous struct, or its type is an anonymous
  struct.
- **`.isResultType`**: Expands to true if the given node is a result type, or it's type is a result type.
- **`.hasBase`**: Expands to true if the type of the given node is a class that has a base type.
- **`.hasDeinit`**: Expands to true if the type of the given node has a de-initializer.
- **`.hasVoidReturnType`**: Expands to true if the type of the given node is a function that returns `void`.
- **`.hasReferenceMembers`**: Expands to true if the type of the given node is a composite type with reference members.

#### AST Index Operator `[]`

Iterable comptime AST nodes can be indexed using the `.[index]` operator, where `index` can be a string literal or an
integer literal. If the `index` key is out of range or does not exist, `null` literal is returned.

- Attribute collection supports this operator where `<index>` is the name of the attribute to lookup.

```c
#const json = T.attributes.["json"];
#if (json) {
  // do something.
}
```

- An attribute with parameters also supports this operator, `<index>` is required to be a string representing the
  parameter name if the parameters are named or integer if the parameters are unnamed.

```c
@hello(10, "world")
...
#const hello = T.attributes.["hello"];
#const id = hello.[1]

@hello(id: 10, name: "world")
...
#const hello = T.attributes.["hello"];
#const name = hello.["name"];
```

- Annotation collections also support comptime index operations where `<index>` is the name of the annotation to get.

```c
struct Hello {
  `id = 10
  `name = "World"
}
...
#const annotations = #{Hello.annotations};
#const id = annotations.["id"]
```