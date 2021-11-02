//  Author: Andrea Tongsak
//  Assignment 3: smallsh
//  CS344: Operating Systems 1

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <signal.h>

// Define Token Buffer and Delimeter
#define TOKEN_BUFFER_SIZE 512     // Token Buffer sie of 512 bytes
#define TOKEN_DELIM " \t\r\n"     // delimiters for space, tab, carriage return, and new line

char *STRTDIR;              // Starting directory
int bg_pid_array[128];      // Array keeping track of background processes pids
int bg_pid_cnt = 0;         // background processes pid count
int inputdirection;         // 1 or ON if direction is "<", 0 or OFF if not
int outputdirection;        // 1 or ON if direction is ">", 0 or OFF if not
char *infile;               // Input filename to READ from
char *outfile;              // Output filename to WRITE to
int status;                 // Exit status of processes
int running = 1;            // Controls main loop by running flag
int fg_mode = 0;            // Foreground-only mode flag, set to OFF
int bg_proc_flg = 0;        // Background process flag, set to OFF

// Signal actions set to empty
struct sigaction SIGINT_action = {{0}};
struct sigaction SIGTSTP_action = {{0}};

/* ***********************************************************
 * switch_fg_bg_mode()
 * Switching between foreground and background modes
 * according to SIGTSTP or CTRL^Z
 * *********************************************************/
void switch_fg_bg_mode() {
    
    char *prompt = ": ";    // set the user prompt
    
    // if it is foregound mode then Exit foreground mode
    if (fg_mode == 1) {
        char *message = "\nExiting foreground-only mode\n";
        write(STDOUT_FILENO, message, 30);
        fg_mode = 0;
    // if it is not then set to foreground mode
    } else {
        char *message = "\nEntering foreground-only mode (& is now ignored)\n";
        write(STDOUT_FILENO, message, 50);
        fg_mode = 1;
    }
    
    // write prompt to user to continue
    write(STDOUT_FILENO, prompt, 2);
}

/* ****************************************************************************
 * fillout_SIGINT_action_struct()
 * Initializes the handling of SIGINT
 * (see Req.8 Signals SIGINT & SIGTSTP)
 * NOTE:
 * SIGINT is a Signal related to process termination
 *        sent by a terminal to the foreground process group when
 *        user types Control-C
 * SIG_IGN specifies that the signal should be ignored.
 *         Send the signal SIGINT to this process when user enters Control-C.
 *         That will cause the signal handler to be invoked
 * REFERECNE: Examples: Catching & Ignore Signals
 * ****************************************************************************/
void fillout_SIGINT_action_struct() {
    // Register SIG_IGN as the signal handler
    SIGINT_action.sa_handler = SIG_IGN;
    
    // Block all catchable signals
    sigfillset(&SIGINT_action.sa_mask);

    // Install our signal handler SIGINT
    sigaction(SIGINT, &SIGINT_action, NULL);
   
}

/* ***************************************************************************
 * fillout_SIGTSTP_action_struct()
 * Initializes the handling of SIGINT and SIGTSTP
 * (see Req.8 Signals SIGINT & SIGTSTP)
 * NOTE:
 * SIGTSTP is issued at a terminal to stop the process group currently running in the foreground.
 *        user types Control-Z
 * SA_RESTART Provide behavior compatible with BSD signal semantics
 *        by making certain system calls restartable across signals
 ************************************************************************/
void fillout_SIGTSTP_action_struct() {
    // Register our switch_fg_bg_mode() as the signal handler got SIGTSTP
    SIGTSTP_action.sa_handler = switch_fg_bg_mode;
    
    // Block all catchable signals while switch_fg_bg_mode() is running
    sigfillset(&SIGTSTP_action.sa_mask);
    
    // Set flag to SA_RESTART
    SIGTSTP_action.sa_flags = SA_RESTART;
    
    // Install our signal handler SIGTSTP
    sigaction(SIGTSTP, &SIGTSTP_action, NULL);
}

/* ***********************************************************
 * remove_bg_processes()
 * Removes pids from the background processes array
 * *********************************************************/
void remove_bg_processes(int pidno) {
    int i;
    for (i = 0; i < bg_pid_cnt; i++) {
        if (bg_pid_array[i] == pidno) {
            while (i < bg_pid_cnt - 1) {
                bg_pid_array[i] = bg_pid_array[i + 1];
                i++;
            }
            bg_pid_cnt--;
            break;
        }
    }
}

/* ***************************************************************************************
 * check_bg_processes()
 * Checks background pids if they are completed; also remove bg pid from the bg pid
 * tracking array.
 * waitpid() suspends execution of the calling process until a child specified by
 *           pid argument has changed state.
 * syntax: pid_t waitpid(pid_t pid, int *status, int options);
 *      Option is WNOHANG: Return immediately if no child has exited.
 * ***************************************************************************************/
void check_bg_processes() {
    
    // Check to see if a background process terminated
    pid_t bg_pid;
    bg_pid = waitpid(-1, &status, WNOHANG);
    
    while (bg_pid > 0) {
        // If so, remove pids from the array
        remove_bg_processes(bg_pid);
        
        // Print the exit status or termination signal
        if (WIFEXITED(status)) {
            printf("background pid %d is done. exit value %d\n", bg_pid, WEXITSTATUS(status));
            fflush(stdout);
        } else if (WIFSIGNALED(status)) {
            printf("background pid %d is done. terminated by signal %d\n", bg_pid, WTERMSIG(status));
            fflush(stdout);
        }
        
        // Check if there are additional bg processes ending, loop if necessary
        bg_pid = waitpid(-1, &status, WNOHANG);
    }
}

/* ***********************************************************
 * replace_pid()
 * Replaces value in orignal string with the new value
 * *********************************************************/
void replace_pid(char *originalStr, const char *searchSubStr, const char *replaceValue) {

    size_t searchSubStr_len = strlen(searchSubStr);         // get the search value length (2)
    size_t replaceValue_len = strlen(replaceValue);         // the replace value length (5)
    char buffer[2048] = { 0 };                              // Initialize temporary buffer to hold modified string
    char *insert_point = &buffer[0];                        // Set insert point to first character in buffer
    const char *tmp = originalStr;                          // Set char pointer temp to source
    
    while (1) {
        // Find occurrences of $$ in the search substring
        const char *p = strstr(tmp, searchSubStr);

        // No more occurrences, copy remaining
        if (p == NULL) {
            strcpy(insert_point, tmp);
            break;
        }

        // Copy part segment before $$
        memcpy(insert_point, tmp, p - tmp);
        insert_point += p - tmp;

        // Copy pid into place
        memcpy(insert_point, replaceValue, replaceValue_len);
        insert_point += replaceValue_len;

        // Adjust pointer, move on
        tmp = p + searchSubStr_len;
    }

    // Write altered string back to the original string
    strcpy(originalStr, buffer);
}

/* ***********************************************************************
 * read_user_input()
 * Takes user input command line.
 * If there are $$ in that user input then replace them with process pid.
 * ***********************************************************************/
char* read_user_input() {
    // Command prompt
    printf(": ");
    fflush(stdout);
    
    // Get user command line
    char *usr_cmd_line = NULL;
    size_t bufsize = 0;
    getline(&usr_cmd_line, &bufsize, stdin);
    
    // Find the 1st occurence of "$$"
    char *p = strstr(usr_cmd_line, "$$");
    // if "$$" found in the user command line then get pid
    // and replace "$$" with the pid
    if (p) {
        char pidStr[6];
        sprintf(pidStr, "%d", getpid());
        replace_pid(usr_cmd_line, "$$", pidStr);
    }
    return usr_cmd_line;
}

/* **********************************************************
 * parse_line2args()
 * Parses user input line then create args array
 * *********************************************************/
char** parse_line2args(char *usr_cmd_line) {
    
    // Break user command line into tokens of user command arguments
    char **usr_args = malloc(TOKEN_BUFFER_SIZE * sizeof(char*)); // hold user command arguments
    char *token;                    // each token
    int position = 0;               // set the initial position of usr_args array
    inputdirection = 0;             // set input direction to OFF
    outputdirection = 0;            // set output direction to OFF
    
    // Get first token from the given user command line
    // Tokens are separated by space, tab or \t, carriage return or \r, new line or \n
    token = strtok(usr_cmd_line, TOKEN_DELIM);
    
    // loop while we still have tokens
    while (token != NULL) {
        
        // For Output Redirection Token
        if (strcmp(token, ">") == 0) {
            outputdirection = 1;                    // turn output direction ON
            outfile = strtok(NULL, TOKEN_DELIM);    // take next token and assign to outfile
            token = strtok(NULL, TOKEN_DELIM);      // move to the next token
            usr_args[position] = NULL;              // clear position of that usr_args
            position++;
            continue;
        }
        
        // For Input Redirection Token
        if (strcmp(token, "<") == 0) {
            inputdirection = 1;                     // turn input direction ON
            infile = strtok(NULL, TOKEN_DELIM);     // take next token and assign to infile
            token = strtok(NULL, TOKEN_DELIM);      // move to the next token
            usr_args[position] = NULL;              // clear position of that usr_args
            position++;
            continue;
        }
        
        // For Background Process Token
        if (strcmp(token, "&") == 0) {
            usr_args[position] = NULL;              // clear position of that usr_args
            if (fg_mode) {
                bg_proc_flg = 0;                    // if in foreground mode, turn background process flag OFF
            } else {
                bg_proc_flg = 1;                    // else turn background process flag ON
            }
            break;
        }
        
        // Add token to user_args
        usr_args[position] = token;
        position++;
        token = strtok(NULL, TOKEN_DELIM);      // move to the next token of usr_cmd_line
    }
    
    // Set last argument to NULL
    usr_args[position] = NULL;          // clear position of that usr_args
    return usr_args;
}

/* ***********************************************************
 * add_bg_pid()
 * Adds pid to the background processes tracker array
 * *********************************************************/
void add_bg_pid(int pidno) {
    //add pid to the bg process tracking array
    bg_pid_array[bg_pid_cnt] = pidno;
    bg_pid_cnt++;
}

/* *****************************************************************
 * execute_other_command()
 * Execute any other commands other than the built-in commands
 * by spawning child processes (See Req.5 Execute Other Commands).
 * *****************************************************************/
int execute_other_command(char **args) {
    // Track pids
    pid_t spawnPID;
    pid_t waitPID;
    
    // Spawn child
    spawnPID = fork();
    
    switch (spawnPID) {
            
    case -1:  // fail to fork() child process
        perror("fork() failed \n");
        exit(1);
        break;
            
    case 0:   // we have child process
        fflush(stdout);
        // if input direction is ON
        if (inputdirection) {
            int file2read = open(infile, O_RDONLY);
            if (file2read == -1) {
                printf("cannot open %s for input\n", infile);
                fflush(stdout);
                exit(1);
            } else {
                // Use dup2 to redirect or point STDOUT_FILENO FD to file2read FD
                if (dup2(file2read, STDIN_FILENO) == -1) {
                    perror("dup2() failed \n");
                }
                close(file2read);
            }
        }
        // If output direction is ON
        if (outputdirection) {
            int file2write = creat(outfile, 0644);
            if (file2write == -1) {
                printf("cannot create %s for output\n", outfile);
                fflush(stdout);
                exit(1);
            } else {
                // Use dup2 to redirect or point STDOUT_FILENO FD to file2write FD
                if (dup2(file2write, STDOUT_FILENO) == -1) {
                    perror("dup2() failed \n");
                }
                close(file2write);
            }
        }
        // If background process is ON
        if (bg_proc_flg) {
            // if input direction is OFF
            if (!inputdirection) {
                int file2read = open("/dev/null", O_RDONLY);
                if (file2read == -1) {
                    printf("cannot set /dev/null for input\n");
                    fflush(stdout);
                    exit(1);
                } else {
                    // Use dup2 to redirect or point STDOUT_FILENO FD to file2read FD
                    if (dup2(file2read, STDIN_FILENO) == -1) {
                        perror("dup2() failed \n");
                    }
                    close(file2read);
                }
            }
            // if output direction is OFF
            if (!outputdirection) {
                int file2write = creat("/dev/null", 0644);
                if (file2write == -1) {
                    printf("cannot set /dev/null for output\n");
                    fflush(stdout);
                    exit(1);
                } else {
                    // Use dup2 to redirect or point STDOUT_FILENO FD to file2write FD
                    if (dup2(file2write, STDOUT_FILENO) == -1) {
                        perror("dup2() failed \n");
                    }
                    close(file2write);
                }
            }
        }

        // If the bg is off, SIGINT action to default for foreground child process
        if (!bg_proc_flg) {
            SIGINT_action.sa_handler = SIG_DFL;
            SIGINT_action.sa_flags = 0;
            sigaction(SIGINT, &SIGINT_action, NULL);
        }
            
        // Now send user command args for execution
        if (execvp(args[0], args)) {
            perror(args[0]);
            exit(1);
        }
    
    default:
        // If background process is OFF
        if (!bg_proc_flg) {
            do {
                // Wait for foreground child processes
                waitPID = waitpid(spawnPID, &status, WUNTRACED);
                if (waitPID == -1) {
                    perror("waitpid");
                    exit(1);
                }
                // if child process exited because a signal was not caught
                if (WIFSIGNALED(status)) {
                    printf("terminated by signal %d\n", WTERMSIG(status));
                    fflush(stdout);
                }
                // if child process is stopped
                if (WIFSTOPPED(status)) {
                    printf("stopped by signal %d\n", WSTOPSIG(status));
                    fflush(stdout);
                }
            } while (!WIFEXITED(status) && !WIFSIGNALED(status));
        // If background process is ON
        } else {
            // Don't wait for background child processes.. but store pids
            printf("background pid is %d\n", spawnPID);
            fflush(stdout);
            add_bg_pid(spawnPID);
            bg_proc_flg = 0; // reset bg process flag in parent
        }

    } // end switch
    return 0;
}

/* ***************************************************************************
 * execute_usr_command()
 * 1. Execute the three built-in commands (cd, status, exit) and ignore
 *    blank user input or input starting with # character comment.
 * 2. Execute any other commands other than the built-in commands
 *    by spawning child processes (See Req.5 Execute Other Commands).
 * ***************************************************************************/
void execute_usr_command(char **args) {
    
    // If it's a blank line or starting with comment hatch char
    if ((args[0] == NULL) || (strchr(args[0],'#'))) {
        // Do nothing...
    } else if (strcmp(args[0], "exit") == 0) {
        // It is "exit", SIGTERM to background processes to terminate all processes
        while (bg_pid_cnt > 0) {
            kill(bg_pid_array[0],SIGTERM);
            remove_bg_processes(bg_pid_array[0]);
        }
        // and set the running shell to no longer active
        running = 0;
    // if user command is for change directory "cd"
    } else if (strcmp(args[0], "cd") == 0) {
        // It is "cd", change directory
        if (args[1] == NULL) {
            if(chdir(getenv("HOME")) != 0) {
                perror("chdir");
            }
        } else {
            if(chdir(args[1]) != 0) {
                perror("chdir");
            }
        }
    // if user command is for exit status "status"
    } else if (strcmp(args[0], "status") == 0) {
        // It is "status" then, get appropriate status
        if (WIFEXITED(status)) {
            printf("exit value %d\n", WEXITSTATUS(status));
            fflush(stdout);
        } else if (WIFSIGNALED(status)){
            printf("terminating signal %d\n", WTERMSIG(status));
            fflush(stdout);
        }
    } else {
        execute_other_command(args);
    }
}

/* ************************************************************************
 * main()
 * Performs the followings
 * 1. Saves current environment - directory location,
 * 2. Initializes the settings related to SIGNALS (SIGINT and SIGTSTP),
 * 3. Runs main loop - checks processes, gets user input, executes commands,
 *    frees memory and resets pointers
 * 4. Restores back the environment
 * ************************************************************************/
int main (int argc, char* argv[]) {
    
    char *usr_input;
    char **args;
    
    // Store Starting Directory
    STRTDIR = getcwd("PWD", 3);
    
    // Fillout SIGNALS Actions Needed
    fillout_SIGINT_action_struct();
    fillout_SIGTSTP_action_struct();
 
    // User Shell Execution Loop
    while(running) {

        // Check background processes if they have completed
        if (bg_pid_cnt > 0) {
            check_bg_processes();
        }

        // Handle user input
        usr_input = read_user_input();
        args = parse_line2args(usr_input);
        execute_usr_command(args);
        
        // Free memory, reset files to NULL
        if (usr_input != NULL) {
            free(usr_input);
        }
        if (args != NULL) {
            free(args);
        }
        infile = NULL;
        outfile = NULL;
    }
    // Reset directory back
    chdir(STRTDIR);
    
    return 0;
}
