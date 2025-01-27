## `stdlib/os`

```c
import "stdlib/os.h"

func main(args: [string]) !:void {
    // execute a command and capture it's output
    var proc = os.capture("ls", "-al", "./www")
    if (proc.wait()) {
        stdout << proc.stdout.readAll()
    }
    else {
        stdout << "error: " << proc.stderr.readAll()
    }
    
    // Execute a shell command capturing it's output using `sh`
    var proc = os.shell("ls", "$MY_DIR", { MY_DIR: "./www" })
    // can wait for proc, and std its i/o
    
    // Execute a shell command, does not capture output
    :sh "echo Hello ${USERNAME}" { USENAME: "awesome" }
    // The above uses the macro `sh` to execute the command
    // The macro simple calls os.execute(cmd, env)
}
```