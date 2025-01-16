# CLI Arguments

`cxy` stdlib comes with a simple CLI arguments parser (see `stdlib/args.cxy`)

### Usage

1. Import the types needed to construct a parser, commands and flags
2. Create some commands that will be used by the application
3. Create an application that will parse and invoke the requested command
4. Add global flags and commands to the application
5. Parse the command line arguments

### Example

Following example show how to craft an app that parses command line arguments.

```c
import { App, Command, command, Param } from "stdlib/args.cxy"

class RateCommand: Command {
   func `init`() {
       // Provide command name and description
       super("rate", "Send a rating to cxy")
   }

   // Handle the command
   @override
   func run(): i32 {
       var stars = super.argument("stars")
       var comment = super.argument("comment");
       println("Rating: ", stars)
       println("Comment: ", comment)
       return 0
   }
}

class RejectCommand: Command {
    func `init`() {
        super("reject", "Why would want to reject")
    }

    func run(): i32 {
        if (!positionals.empty()) {
            const why = positionals.[0];
            println("Reject with a lame reason: ", why)
        }
        return 0
    }
}

func main(args: [string]): !void {
    // Create application instance
    var app = App("rater".s, "1.0.0".s, "Application used to test stdlib/args.cxy".s, "rate".s);

    // Build parser
    app(
       // Optionally provide global arguments, as many as one wants
       Param{
            name: "user".s, sf: 'u',
            desc: "The username of the person providing the comment".s,
            option: false
       },
       Param{
            name: "location".s,
            desc: "The location where the user is location".s,
            def: "".s
       },
       // Provide arguments, they must come after the global flags, otherwise compilation will fail
       command[RateCommand](
            Param{
                name: "stars".s, sf: 's', desc: "The number of stars you are rating cxy".s, option: false
            },
            Param{
                name: "comment".s, desc: "The rating comment".s, def: "".s
            }
       ),
       // TODO support positionals
       command[RejectCommand]()
    )

    // Parse the command line arguments
    app.parse(args)
}
```

### Example Output

1. Show application help
   ```
   # Show help
   > app help
   rater v1.0.0
   Application used to test stdlib/args.cxy
   
   Usage:  rater [command]
   
   Available Commands:
     rate    Send a rating to cxy
     help    Show application or command help
     reject  Why would want to reject
     version Show application version
   
   Flags:
       -h, --help            Show application or current command help
           --location        The location where the user is location
       -u, --user (required) The username of the person providing the comment
   
   Use "rater [command] --help" for more information about a command
   ```
2. Show help specific to a command
   ```
   # Show rate help ( or `app rate --help`)
   > app help rate
   Usage:
     rater rate [flags]
   
   Flags:
       -s, --stars (required) The number of stars you are rating cxy
           --comment          The rating comment
   
   Global Flags:
       -h, --help            Show application or current command help
           --location        The location where the user is location
       -u, --user (required) The username of the person providing the comment
   ```