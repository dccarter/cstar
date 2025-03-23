# Error Handling

`cxy` supports raising and handling exceptions with the help of the `exception`, `raise`,
`catch` and `discard` keywords.

## Declaring Exceptions

- **ONLY** declared exceptions can be raised
- Exceptions can be declared using the `exception` declaration syntax.
- An exception declaration dictates the structure of the exception
    - It has a name, constructor arguments and an exception body which
      returns the exception message
- The exception body **MUST** always return a `string` type

### Simple Declarations

```
param  : <name> ':' <Type> ('=' <expr>)?
params : (<param> (',' <param>)*)?
body   : (( '=> <expr>) | <block>)
excpt  : 'exception' <name> ( '(' <params>? ')' )? <body>?
```

* Exception are declared with the `exception` keyword
* The exception name is required and follows identifier naming rules
* Exception parameters are optional. If specified, they are just like regular function
  parameters.
    * i.e each parameter follows the grammar, `<name>: <Type> ('=' <expr>)?`.
* The body is also optional. If specified, it can either be a block of statements (or empty)
  or an expression

```c
exception Error1
// raise Error1()
exception Error2 => "Error2 message"
// raise Error2()
exception Error3 { return "Error3 message" }
//raise Error3()
exception Error4(msg: string) => msg
//raise Error4("Hello World")
exception Error5(id: i32, msg: String = null) {
    if (msg == null)
        msg = f"Hello number ${id}"
}
// raise Error5(100)
// ex.what() returns "Hello number 100"
```

* :information_source: - `Error5` in the examples above has parameter `msg` of type `String`. This is
  the only way to model exception state.

### Raising Exceptions

```
raise : 'raise' (<Type>( <args> ))?
```

* All function types that raise an exception **MUST** explicitly return an exception result
  type. Exception result type are regular types prefixed with the `!` token.
    * For example, the following function returns `void` but raises an exception, so the return
      type is prefixed with `!`.
      ```cxy
      func crash() !void { // something that raises }
      ```
* Exceptions can be raised with the raise keyword followed by the exception to raise (
  optional)
* Within catch blocks, the caught exception can be bubbled up using the `raise` statement
  without the expression

```cxy
exception IOError

func crash(x: i32): !i32 {
    if (x == 0)
        raise IOError()
    return x
}

func main(): !void {
    crash(0) catch {
        // re-raise the caught exception up to main's caller
        raise
    }
}
```

### Catching Exceptions

* Exceptions can be handled using the `catch` binary operator, the left hand side of the
  operator being the handler.
* `catch` operator is only valid if the left hand side of the expression raises an exception.
  Compilation will fail if that is not the case.
* The expression handler can be one of the following depending on context
    * `discard` - this will just discard (or ignore) the exception. Can only be used if the
      return value of the expression is meant to be discarded too.
    * a default value if the expression value type is `void`.
    * a block of regular statements
        * Default expression value can be return by yielding a value using the `yield` keyword
        * `raise` can be used to re-raise the expression that was caught
        * :warning: If the value of the expression is none `void` and used, a default value
          must be yielded.
        * The macro `ex!` can be used to access the caught exception.
            * :no_entry: `cxy` dynamic casting is not implemented yet, care must be
              exercised when casting the caught exception to concrete exception type.

#### Default Handling

If an expression that raises an exception is used without a `catch`, the expression will
be bubbled up to the caller. This means that the function using the expression should also
be marked as raising an exception.

```
exception NullPointerError
exception InvalidOperationError

func say(msg: string): !void {
    if (msg)
        println(msg)
    else
        raise NullPointerError()
}

func sayer(id: i32, msg: string): !i32 {
    if (id != 2)
        raise InvalidOperationError()
    
    // Exception not handled, it will be bubbled up
    say(msg)
    
    if (id == 0)
        return 100
    
    // Catch the exception and re-raise it
    say(null) catch {
        println( "Ooops" ) 
        raise
    }
}

func main() {
    // Call a function that raises, if the exception is raised, ignore it
    say("Hello world") catch discard
    
    // Handle exception
    say("Hello world") catch { println( "Unhandled exception" ) }
        
    // If sayer raises, default id to 0
    var id = sayer(2, "Hellow World") catch 0
    
    // If the expression value is not used, it's okay to not provide a default
    // value
    sayer(2, "Hello World") catch discard
    
    id = sayer(2, "Ok") catch {
        if (id == 0)
            // bubble up the exception
            raise
        // Yield a default value
        yield 100
    }
}
```

### Stack Trace

Exception have a stack call trace showing the origin of the exception, example

```cxy
exception AgeError(msg: String) => msg.str()

struct User {
    - name: String;
    - age = 16 as i32;
    func `init`(name: String) { this.name = &&name }

    func drink() : !void {
        if (age < 21)
            raise AgeError(f"${name} is under age")
        println("Cool brew")
    }
}

func main(): !void {
    var user = User("Carter");
    user.drink()
}
```

Stack trace:

```
AgeError("Carter is under age") at:
 User::drink (/Users/dccarter/projects/cxy/examples/hello.cxy:15)
 main (/Users/dccarter/projects/cxy/examples/hello.cxy:21)

[1]    32032 abort      ./app
```