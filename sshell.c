#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <dirent.h>

#define CMDLINE_MAX 512
#define PATH_MAX 4096

// Structs to hold args to run exec
struct Program {
  // Max args 16 + 1 extra spot for NULL token
  char *args[17];
  // Main arg (argv[0])
  char *first;
  // Index of array of args (used to determine # args
  int index;
};

// Queue implemented with circular array
struct Pipeline {
  // Max amount of pipes (3) with 4 commands + 1 extra spot for NULL token (the queue itself)
  struct Program cmds[5];
  // Helper array to store commands with unparsed args (4 commands + 1 extra for NULL token)
  char *unparsed_cmds[5];
  int num_pipes;
  int num_cmds;
  // Helper indices to point to spots in the array
  int head;
  int tail;
};

// Return number of commands in pipeline of commands
int PipelineCmdCount(struct Pipeline pline) {
  return pline.num_cmds;
}

// Return command in front of line of pipeline
struct Program Front(struct Pipeline pline) {
  return pline.cmds[pline.head];
}

// Pop command from Queue
void Pop(struct Pipeline *pline) {
  pline->head++;
  pline->num_cmds--;
}

// Push command into Queue 
void Push(struct Pipeline *pline, struct Program p) {
  pline->cmds[pline->tail] = p;
  pline->tail++;
  pline->num_cmds++;
}

// Check if the pipeline of commands has too many args
// 1 = Too Many; 0 = Enough
int TooManyArgs(struct Pipeline pline) {
  int i;
  for (i = 0; i < PipelineCmdCount(pline); i++) {
    if (pline.cmds[i].index == 17)
      return 1;
    i++;
  }
  return 0;
}

// Count number of pipse from cmd line (if any)
// and store value in pipeline struct
void CountPipes(char *cmd, struct Pipeline *pline) {
  int amount = 0;
  int len = strlen(cmd);
  int i;
  for (i = 0; i < len; i++) {
    if (*(cmd+i) == '|') {
      amount++;
    }
  }
  pline->num_pipes = amount;
}

// Checks if the string contains only white spaces
int OnlyWhiteSpaces(char *str) {
  
  int len = strlen(str);
  int i;
  for (i = 0; i < len; i++) {
    if (*(str+i) != ' ') {
      return 0;
    }
  }
  return 1;
}

// Handles output redirection by detecting parsing errors
// or separating valid output file from command pipelin
char *OutputRedirect(char *cmd, int IsRedirectErr) {
  // Check if command starts with ">"
  char first_char = *cmd;
  char *file_out;
  // No command found
  if (first_char == '>') {
    fprintf(stderr, "Error: missing command\n");
    return NULL;
  }
  
  // Strtok depending on special output redirect char
  if (IsRedirectErr) {
    cmd = strtok(cmd, ">&");
    file_out = strtok(NULL, ">&");
  }  else {
       cmd = strtok(cmd, ">");
       file_out = strtok(NULL, ">");
  }

  // cmd is white space from strtok, no command
  if (OnlyWhiteSpaces(cmd)) {
    fprintf(stderr, "Error: missing command\n");
    return NULL;
  }  else if (file_out == NULL || OnlyWhiteSpaces(file_out)) {
       // No output file exists
       fprintf(stderr, "Error: no outputfile\n");
       return NULL;
  }

  // Output file contains a pipe, report error
  if (strstr(file_out, "|")) {
    fprintf(stderr, "Error: mislocated output redirection\n");
    return NULL;
  }
  
  // Valid output redirection
  return file_out;
}

// Check if the command entered is a built in one
int BuiltInCmd(char *cmd) {
  // Values returned depending on command
  int EXIT = 2;
  int PWD_CD_OR_SLS = 1;
  int NONE = 0;

  if (!strcmp(cmd, "exit")) {
    // exit
    fprintf(stderr, "Bye...\n");
    fprintf(stderr, "+ completed 'exit' [0]\n");
    return EXIT;
  }  else if (!strcmp(cmd, "pwd")) {
       // pwd
       char cwd[PATH_MAX];
       getcwd(cwd, sizeof(cwd));
       printf("%s\n", cwd);
       fprintf(stderr, "+ completed 'pwd' [0]\n");
       return PWD_CD_OR_SLS;
  }  else if (strstr(cmd, "cd ") != NULL) {
       // cd

       // Extract command and directory
       char *cd = strtok(cmd, " ");
       char *arg = strtok(NULL, " ");

       // Directory can not be accessed
       if (chdir(arg) == -1) {
         fprintf(stderr, "Error: cannot cd into directory\n");
         fprintf(stderr, "+ completed '%s %s' [1]\n", cd, arg);
         return PWD_CD_OR_SLS;
       }

       // Directory accessed
       fprintf(stderr, "+ completed '%s %s' [0]\n", cd, arg);

       return PWD_CD_OR_SLS;
  }  else if (!strcmp(cmd, "sls")) {
       // Set up variables to access info
       DIR *dirp;
       struct dirent *dp;
       struct stat sb;

       // Directory could not be opened
       if ((dirp = opendir(".")) == NULL) {
         fprintf(stderr, "Error: cannot open directory\n");
         fprintf(stderr, "+ completed 'sls' [1]\n");
         return PWD_CD_OR_SLS;
       }

       // For each directory entry, print the entry and its size
       while ((dp = readdir(dirp)) != NULL) {
         stat(dp->d_name, &sb);
         if (strcmp(dp->d_name,".") && strcmp(dp->d_name,"..")) {
           printf("%s (%lld bytes)\n", dp->d_name, (long long) sb.st_size);
         }
       }

       fprintf(stderr, "+ completed 'sls' [0]\n");
       return PWD_CD_OR_SLS;
  
  }  else {
       // not a built in command
       return NONE;
  }
}

// Parse the commands into its args and store
// its contents into Program struct
void CmdParse(struct Program *p, char *cmd) {
  char *tok = strtok(cmd, " ");
  // Retrieve first arg from command  
  p->first = tok;
  // Used to fill array and determine if too many args
  p->index = 0;

  // Store all arguments into array
  while (tok != NULL) {
    if (p->index == 17) { // Too many arguments
      break;
    }
    p->args[p->index] = tok;
    tok = strtok(NULL, " ");
    p->index++;
  }
}

// Parsee the commands in the pipeline and store
// the unparsed commands in array to later parse them
void PipeParse(struct Pipeline *pline, char *cmd) {
  // To index through array
  int index = 0;
  // To account for special meta character (if used)
  char *tok = strtok(cmd, "|&");
  pline->unparsed_cmds[index] = tok; 
  index++;
  while (tok != NULL) {
    tok = strtok(NULL, "|&");
    pline->unparsed_cmds[index] = tok;
    index++;
  }
}

// Execute a single command and does output redirection
// if needed, establishing errors if a command is not found
// or if the output file to be written to cannot be opened
void ExecuteSingleCommand(struct Pipeline *pline, char *cmd_copy, char *output_file, int IsRedirectErr) {
  pid_t pid;
  pid = fork();
  // Fork child process to use dup2 and exec
  if (pid == 0) {
    int fd;

    // There is output redirection
    if (output_file != NULL) {
      fd = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
      // >& character, redirect stderr to output file
      if (IsRedirectErr)
        dup2(fd, STDERR_FILENO);
      // Othewise, regular stdout to output file
      dup2(fd, STDOUT_FILENO);
      close(fd);
    }

    // Command not found
    if (execvp(Front(*pline).first, Front(*pline).args) == -1) {
      fprintf(stderr, "Error: command not found\n");
      exit(1);
    }
  }  else if (pid > 0) {
       // Parent to wait for child
       int status;
       waitpid(-1, &status, 0);
       fprintf(stderr, "+ completed '%s' [%d]\n", cmd_copy, WEXITSTATUS(status));
  }  else {
       //Fork went wrong
       perror("Error: ");
       exit(1);
  }
  // Command done, pop from the queue
  Pop(pline);
}

// Execute the commands involved in the pipeline
void ExecutePipedCommands(struct Pipeline *pline, char *cmd_copy, char *output_file, int IsRedirectErr, int PipeErr) {
  // If 1, then command to be run is not the first nor last in pipeline
  int IsMiddle = 0;
  // Use to access elements of pipe
  int index = 1;
  int numCmds = PipelineCmdCount(*pline);
  // Establish fds for piping
  int fd[2*pline->num_pipes];
  // Array to hold children PIDs
  pid_t childPIDS[numCmds];
  int pid_index = 0;
  int i; // indexing for loops

  // Set up the pipes
  for (i = 0; i < 2*pline->num_pipes; i+=2) {
    pipe(fd+i);
  }

  // Main piping loop
  while (PipelineCmdCount(*pline)) {
    // Still more commands (Between ends of pipe)
    if (IsMiddle && PipelineCmdCount(*pline) > 1) {
      pid_t pid;
      pid = fork();
      // Child to execute command
      if (pid == 0) {
        // Set the pipe
        // |& character
        if (PipeErr)
          dup2(fd[index], STDERR_FILENO);
        // Access read of previous pipe to put in write of next pipe
        dup2(fd[index], STDOUT_FILENO);
        dup2(fd[index-3], STDIN_FILENO);
        // Close fds
        for (i = 0; i < 2*pline->num_pipes; i++) {
          close(fd[i]);
        }
        // Error in execution
        if (execvp(Front(*pline).first, Front(*pline).args) == -1) {
          fprintf(stderr, "Error: command not found\n");
          exit(1);
        }
      }  else if (pid > 0) {  // Parent
           // Get child PID for waitpid
           childPIDS[pid_index] = pid;
           pid_index++;
      }  else {
           // Fork went wrong
           perror("Error: ");
           exit(1);
      }
    }  else if (PipelineCmdCount(*pline) > 1) {
         // Still more commands (Start of pipe) 
         pid_t pid;
         pid = fork();
         // Child to execute command
         if (pid == 0) {
           // Set the pipe
           // |& character
           if (PipeErr)
             dup2(fd[index], STDERR_FILENO);
           dup2(fd[index], STDOUT_FILENO); // Regular pipe
           // Close fds
           for (i = 0; i < 2*pline->num_pipes; i++) {
             close(fd[i]);
           }
           // Error in execution
           if (execvp(Front(*pline).first, Front(*pline).args) == -1) {
             fprintf(stderr, "Error: command not found\n");
             exit(1);
           }
         }  else if (pid > 0) {  // Parent
              // Get child PID for waitpid
              childPIDS[pid_index] = pid;
              pid_index++;
         }  else {
            // Fork went wrong
            perror("Error: ");
            exit(1);    
         }
         // Now working in between pipe
         IsMiddle = 1;
    }  else {
         // End of pipe (Last command)
         pid_t pid;
         pid = fork();
         // Child to execute command
         if (pid == 0) {
           // >& redirection
           if (output_file != NULL && IsRedirectErr) {
             int fd_err = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
             dup2(fd_err, STDERR_FILENO);
             close(fd_err);
           }
           if (output_file != NULL) { // Regular redirection
             int fd_out = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
             dup2(fd_out, STDOUT_FILENO);
             close(fd_out);
           }
           // Set the pipe (get read of current pipe)
           dup2(fd[index-1], STDIN_FILENO);
           // Closee fds
           for (i = 0; i < 2*pline->num_pipes; i++) {
             close(fd[i]);
           }
           // Error in execution
           if (execvp(Front(*pline).first, Front(*pline).args) == -1) {
             fprintf(stderr, "Error: command not found\n");
             exit(1);
           }
         }  else if (pid > 0) {  // Parent
              // Get child PID for waitpid
              childPIDS[pid_index] = pid;
              pid_index++;
         }  else {
            // Fork went wrong
            perror("Error: ");
            exit(1); 
         }
    }
    // Pop commands from pipeline
    Pop(pline);
    // Get write index of next pipe
    if (PipelineCmdCount(*pline) > 1) {
      index += 2;
    }
  }  // End of while loop
  // Close all fds for parent process
  for (i = 0; i < 2*pline->num_pipes; i++) {
    close(fd[i]);
  }

  int status;
  int j = 0; // index variable for loop
  int status_arr[numCmds]; // To store statuses for printing
  for (j = 0; j < numCmds; j++) {
    // Get statuses based on chronological child exec
    waitpid(childPIDS[j], &status, 0);
    // Store it
    status_arr[j] = WEXITSTATUS(status);
  }

  // Print the completion message
  fprintf(stderr, "+ completed '%s' ", cmd_copy);
  for (j = 0; j < numCmds; j++) {
    fprintf(stderr, "[%d]", status_arr[j]);
  }
  fprintf(stderr, "\n");
} 

// Main shell method
int main(void)
{
  char cmd[CMDLINE_MAX];
  while (1) {
    char *nl;

    /* Print prompt */
    printf("sshell@ucd$ ");
    fflush(stdout);

    /* Get command line */
    fgets(cmd, CMDLINE_MAX, stdin);

    /* Print command line if stdin is not provided by terminal */
    if (!isatty(STDIN_FILENO)) {
      printf("%s", cmd);
      fflush(stdout);
    }

    /* Remove trailing newline from command line */
    nl = strchr(cmd, '\n');
    if (nl)
      *nl = '\0';

    // Command only white spaces, reprompt
    if (OnlyWhiteSpaces(cmd))
      continue;

    //Preserve full command to print after result
    char *cmd_copy = malloc(sizeof(cmd));
    // Error in malloc
    if (cmd_copy == NULL) {
      perror("Error: ");
      exit(1);
    }
    cmd_copy = strcpy(cmd_copy, cmd);

    // Check for built in command input
    int option = BuiltInCmd(cmd);
    if (option == 2) {
      // exit command
      break;
    }  else if (option == 1) {
         // cd or pwd command
         continue;
    }
    // option == 0 for none
    
    // Handle output redirection
    char *output_file = NULL;
    // Check if special redirect
    int IsRedirectErr = 0;
    if (strstr(cmd, ">&")) {
      IsRedirectErr = 1;
    }
    
    if (strstr(cmd, ">")) {
      // Get extract output file if it exists
      output_file = OutputRedirect(cmd, IsRedirectErr);
      // syntax error in redirection
      if (output_file == NULL)
        continue;
      // Remove any leading white spaces from output file
      output_file = strtok(output_file, " ");
      // Check if file can be opened
      int f = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
      if (f == -1) {
        fprintf(stderr, "Error: cannot open output file\n");
	continue;
      }
      close(f);
    }

    //New pipeline of commands
    struct Pipeline pline;
    //Set to all 0 first
    memset(pline.cmds, 0, sizeof(struct Program)*5);
    pline.head = 0;
    pline.tail = 0;
    
    //Indicator if cmd line has pipe(s)
    int hasPipe = 0;
    CountPipes(cmd, &pline); 
    if (pline.num_pipes > 0) 
      hasPipe = 1;
    //Check if there is |&
    int PipeErr = 0;
    if (strstr(cmd, "|&")) {
      PipeErr = 1;
    }

    // Split commands if there are pipes
    PipeParse(&pline, cmd);
    
    // To access elements of array of unparsed commands
    int i = 0;
    // Parse piped commands and store into the queue
    while (pline.unparsed_cmds[i] != NULL) {
      struct Program p;
      memset(p.args, 0, sizeof(char*)*17);
      CmdParse(&p, pline.unparsed_cmds[i]);
      Push(&pline, p);
      i++;
    }

    // Check if any of commands in pipeline has too many arg
    if (TooManyArgs(pline)) {
      fprintf(stderr, "Error: too many process argumnets\n");
      // Clear pipeline
      while (PipelineCmdCount(pline)) {
        Pop(&pline);
      }
      continue;
    }

    // For any amount of commands with pipes (n), 
    // the amount of pipes is n - 1
    if (pline.num_cmds - 1 != pline.num_pipes && hasPipe) {
      fprintf(stderr, "Error: missing command\n");
      // Clear pipeline
      while (PipelineCmdCount(pline)) {
        Pop(&pline);
      }
      continue;
    }

    // Only one command, just execute it
    if (pline.num_cmds == 1) {
      ExecuteSingleCommand(&pline, cmd_copy, output_file, IsRedirectErr);
    }

    // Piping involved, do it properly
    if (pline.num_cmds > 1) {
      ExecutePipedCommands(&pline, cmd_copy, output_file, IsRedirectErr, PipeErr);
    }

    free(cmd_copy);
  }
  return EXIT_SUCCESS;
}
