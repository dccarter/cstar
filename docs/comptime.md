# Comptime

`cxy` support compile time code evaluation backed by builtin macros and comptime AST
properties.

### Macros

- **`assert!(cond)`**: Creates a runtime assertion
    - If the assertion fail, it will print out the location and abort the application
      ```c
      const a = false;
      assert!(a)
      ```
- **`ast_list_add!(list, node)`**: Used to add an AST not to a list of nodes
    - This is useful when dynamically building an ast
      ```c
      #const list = mk_ast_list!();
      ast_list_add!(list, true)
      ast_list_add!(list, "Hello World")
      println(#{list})
      // same as println(true, "Hello World")
      ```
- **`base_of!(T)`**: Gets the base of the given type if any. If the type does not have
  a base, compilation will fail. Note that `T` must be a typeinfo node.
    ```c
    var car: base_of!(#Mazda) = ...
    ```
- **`column!`**: Expands to the current column number in the source file
- **`line!`**: Expands to the current line number in the source file
- **`file!`**: Expands to the current file path being compiled

- **`cstr!(str)`**: Converts the type of the given string literal to a c-style string type.
  i.e, changes the type of str (which **MUST** be `string` to `^const char`)
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