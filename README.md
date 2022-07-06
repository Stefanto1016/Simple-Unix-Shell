# Introduction
The objective of this project was to create a simple shell in
order to utilize and better understand the system calls that are
involved in making a shell work. With this shell, users can
provide commands to the command line so that they may be executed
in a proper and orderly fashion.

## Data Structures
There are two main data structures that are involved in this shell
implementation. The first one is a struct that contains the following:

- An array of strings that can hold up to 17 strings to account for
the maximum 16 arguments of a command plus an extra space for **NULL** from 
**strtok()**
- The first argument in the command as a string
- An integer index used to store the arguments into the array and serve
as an indicator for the amount of arguments

The rationale behind using this data structure is so that the struct
contains everything we need in order to use **execvp()** which can execute
programs based on their **$PATH** environment variable.

The second main data structure involved is a struct that serves as the
queue for the pipeline of commands which contains the following:

- An array that holds up to 4 instances of commands with their 
arguments and an extra space for **NULL** from **strtok()** (ie: it holds the
first data structure mentioned above)
- A helper array used to temporarily store the unparsed commands and their
arguments from a piped command
- Data variables used to indicate the number of pipes/commands involved
- Head and tail indices to insert the commands into the main array

The rationale behind using this data structure is to match the FIFO
nature of piping. The commands and their arguments will be pushed into
this queue in order of left to right and when it comes time to execute
piping, they will be executed in order and popped from the queue.

The queue is implemented as a circular array as it is an easy form of
implementation as opposed to a queue implemented using a doubly linked
list for example. With this data structure, we have some typical functions
like **Push()**, **Pop()**, and **Front()** to update and access the contents
in *O(1)* time. It is a very naive implementation that features no error
managemnt as the shell program does not put the queue in a position that
causes errors, with the array being just large enough to hold the max
amount of piped commands.

## How the shell works
### Step 1: Check if command actually has characters
The shell begins by prompting the user to provide input. The input
is then stripped of its trailing newline character and if the input is
empty, then the program swiftly responds by reprompting the user to
provide another input. Once the user provides input that does not consist
of all white space characters.

### Step 2: Check for built-in commands
Before checking if the command is one of the shell's built-in commands,
we make a string copy of the command so that we can use it for completion
messages, as the original command will be altered through **strtok()** if
applicable.

Now we can check if the command is a built-in command using
a function that returns an integer. For single argument commands like 
**exit**,  **pwd**, and **sls**, executing the command is as simple as
checking if the command input matches the command in question and from
there, utilizing the right system calls to get the information we need
and print the desired outputs and completion messages. For **cd**, we
must recognize that the command input has the string "*cd *" and from
there, using strtok to extract "*cd*" from its argument. If the system
call to access the directory (the argument), then an error message is
printed. Otherwise, the directory is accessed and the completion
message ensues. If the command input does not match any of the above, it
will simply return an integer.

Depending on the integer returned, we obtain different behaviors. **exit**
requires the program to stop so it will return a value that allows the
shell to stop. **sls**, **pwd**, and **cd** however, will return a value
that allows to shell to reprompt the user for another command after
having finished one. If none of the built-in functions were called, then
the function returns an integer that allows the shell to move on to the
next step.

### Step 3: Preparing Output Redirection
First, we check the command to see if it contains the special meta-
character, "**>&**". If we do, we set up a boolean variable that will
allow standard error redirection for later. From there, we can check
to see if the command has the "**>**" character for redirection and set
up the output file accordingly using a function that returns a pointer.

If there is an error in trying to parse the output file (eg: no output
file, missing command, etc.) then the function will print an error 
message and return a **NULL** pointer which causes the shell to reprompt 
the user for further input. Otherwise, a valid output file is found and
parsed from the commands beforee it. These commands will later be used
to obtain the commands and their arguments for the pipeline.

One last check is made to ensure that the file can be opened and if it
cannot, then an error message is sent and the user is reprompted for
another command.

### Step 4: Setting up the Pipeline
Here, we create a new pipeline and set up the head and tail indices
for data assignment. We first check the command to determine if
it has pipes and/or the special meta-character "**|&**" and set up
boolean values that will allow for specific command execution later.
We also determine the number of pipes involved in the command and
store the data for use.

From there, we can start parsing the command of its pipes using
**strtok()**. Because **strtok()** alters the original command, we
store the unparsed commands and their arguments into the helper
array of the queue so that we may parse them soon.

Now with the helper array of unparsed commands and their arguments,
we can parse them in a loop and store their data in the first data
structure mentioned in this report. With this data structure set up
to be able to run **execvp()**, we push it into the queue as part of
the pipeline of parsed commands.

### Step 5: Last Checks before Execution
Two checks are implemented to ensure that the command input provided
by the user is valid. The first involves checking the pipeline of its
commands and ensuring that none contain more than 16 arguments. The
second involves checking for missing commands if pipes are being used
such that the number of pipes used must always be one less the amount
of commands provided. If the pipeline fails either one of these tests,
then its queue contents will be cleared, an error message will appear,
and the user will be reprompted for input.

### Step 6: Command Execution
Now we can begin with the execution of the command input which is
split into two separate functions.

The first function will execute a single command that may or may not
have output redirection. It does so first by forking the program into a 
child and parent process. The child process takes care of adjusting the
output to a file if applicable using **dup2()** and will execute the 
command using **execvp()**. The parent process waits for the child
to finish and prints the completion message accordingly. Once all is said
and done, the queue is popped of its only element and the shell reprompts
the user for further command input.

The second function involves the execution of piped commands. It begins by
setting up the following pieces of data:

- **IsMiddle** boolean value to indicate if we're in between the pipes
- A special index to access elements of the pipe
- Pipe/file descriptors depending on the number of pipes used
- Array of children PID to print completion statuses sequentially

The function itself does the piping in a while loop that continues until
the pipeline is all out of commands to execute. There are three main ways
the commands are executed in this loop.

**1.** At the start of the pipeline, we activate the middle if statement
which only needs to replace standard output/error with the write side of
the first pipe. From there, the child can execute the command while the
parent collects the PID of said child. The boolean value **IsMiddle**
then becomes 1 so that now we can work in between the pipes and use the
first if statement (if there are more than 2 commands piped). Lastly, a
command is popped from the queue and the index to access the pipes is
updated accordingly depending if more commands await.

**2.** In between the pipes, we activate the first if statement and now
have to take the standard input from the read side of the current pipe and
place the standard output/error into the write side of the following pipe.
From there, the child can execute the command while the parent collects the
child PID. Finally, a command is popped from the queue and the index to
access the pipes is updated accordingly depending if more commands await.

**3.** At the end of the pipe, we have to consider output redirection
either from standard output or error. Other than that, we just have to
obtain the data from the read side of the last pipe and then have the
child execute the last command. The parent can then collect the last child
PID as the queue pops the last command, thus ending the while loop.

After the execution of the piped commands, we utilize the children PIDs
to store the statuses accordingly into an array. With this array, we can
finally print the completion message, therby finishing the command input
request and allowing the shell to reprompt the user once more.

## Final Comments
Testing this shell proved to be quite the task as there are limitless
amounts of input that the shell can receive and react to. Making sure
the shell works was simply a matter of making sure the output matched
with the reference shell provided by the professor. To start off, the
shell was tested with all the commands shown in the project page and
from there, devising additional plausible inputs to be tested. Making
sure that the shell produces the right output with valid inputs was the
first priority. From there, it can be tested with invalid inputs using a
combination of pipes and output redirection to see if it matches with the
reference shell. For the most part, this shell implementation appears to
work fairly consistently with valid inputs, but when invalid inputs are
involved and error messagees arise, they are sometimes out of place compared
to the reference shell. 
