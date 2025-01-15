# Comptime

`cxy` support compile time code evaluation backed by builtin macros and comptime AST not
attributes.

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
      ```
- **`base_of!(T)`**: Gets the base of the given type if any. If the type does not have
  a base, compilation will fail. Note that `T` must be a typeinfo node.