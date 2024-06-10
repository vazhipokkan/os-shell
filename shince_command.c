#include <unistd.h>   // For pipe(), dup2(), execvp() and fileno()
#include <sys/wait.h>  // For wait() and related macros
#include <stdio.h>     // For I/O functions
#include <sys/types.h> // For pid_t type
#include <stdlib.h>
#include <fcntl.h>     // For open() function
#include <string.h>    // For strdup() function
#include "shell_builtins.c"


/* Command Data Structure */

// Describes a simple command and arguments
typedef struct _SimpleCommand {
    int num_args;   // Number of arguments
    char **args;    // Array of arguments
} SimpleCommand;

void insertArgument(SimpleCommand *simCmd, char *argument) {
    simCmd -> args = realloc(simCmd -> args, sizeof(char*) * (simCmd -> num_args + 1));
    if (!simCmd -> args) {
        perror("realloc");
        exit(EXIT_FAILURE);
    }
    simCmd -> args[simCmd -> num_args++] = argument;
}

SimpleCommand* NewSimCmd(){
    SimpleCommand* SimCmd = malloc(sizeof(SimpleCommand));
    if (!SimCmd) {
        perror("malloc");
    }
    SimCmd -> num_args = 0;
    SimCmd -> args = NULL;
    return SimCmd;
}

// Describes a complete command with input/output redirection if any.
typedef struct _Command {
    int num_simCmds;  // Number of simple commands
    SimpleCommand **simCmds;  // Array of simple commands
    char *outFile;
	int num_inFiles;
    char *inFiles[5];//maximum number of infiles we shall give is 5
	//somehow we need to enforce this
    char *errFile;
    int background;
	int out_append;
	int err_append;
} Command;

void insertSimpleCommand(Command *Cmd, SimpleCommand *simCmd) {
    Cmd -> simCmds = realloc(Cmd -> simCmds, sizeof(SimpleCommand*) * (Cmd -> num_simCmds + 1));
    if (!Cmd -> simCmds) {
        perror("realloc");
        exit(EXIT_FAILURE);
    }
    Cmd -> simCmds[Cmd -> num_simCmds++] = simCmd;
}

Command* NewCmd(){
	int i;    
	Command* Cmd = malloc(sizeof(Command));
    if (!Cmd) {
        perror("malloc");
    }
    Cmd -> num_simCmds = 0;
    Cmd -> simCmds = NULL;
	Cmd -> num_inFiles = 0;
	for(i=0;i<5;i++)
    	Cmd -> inFiles[i] = NULL;

    Cmd -> outFile = NULL;
    Cmd -> errFile = NULL;
    Cmd -> background = 0;
	Cmd -> out_append =0;
	Cmd -> err_append =0;
    return Cmd;
}

int merger(FILE *mergedFile, char** strings, int num_inFiles)
{
	int i;
	for (i = 0;i<num_inFiles;i++)
	{
		char ch;
		FILE *inputFile = fopen(strings[i], "r");
        if (inputFile == NULL) {
            perror("Error opening input file");
            fclose(mergedFile);
            exit(EXIT_FAILURE);
        }

		// Copy contents of current input file to merged file
        while ((ch = fgetc(inputFile)) != EOF) {
            fputc(ch, mergedFile);
        }

        // Close the current input file
        fclose(inputFile);
	}
	//return the file descriptor of mergedfile
	return fileno(mergedFile);
}

void execute(Command *Cmd) {
    
    int tmpin = dup(0); // Save stdin
    int tmpout = dup(1); // Save stdout
	int err_fd = dup(2); //Save stderr
    int numsimplecommands = Cmd -> num_simCmds;
    SimpleCommand **scmd = Cmd -> simCmds;
    char *outFile = Cmd -> outFile;
    char *errFile = Cmd -> errFile;
    int background = Cmd -> background;
	int out_append = Cmd ->out_append;
	int err_append = Cmd ->err_append;
	//Cmd->infiles to be take in one by one and merged to form one file and that file needs to be stored 		//in infile
	int num_inFiles =Cmd->num_inFiles;  
	//********************************************************************
	//implementing error redirection
	if(errFile)
	{
		if(err_append)
			err_fd = open(errFile, O_WRONLY | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR);
		else
			err_fd = open(errFile, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
	}
	// Redirect stderr to error file
    if (dup2(err_fd, STDERR_FILENO) == -1) {
        perror("Error redirecting stderr");
        exit(EXIT_FAILURE);
    }
	//********************************************************************
	
	//********************************************************************	
	//merge input files if there are many
	int fdin=0;
	if(num_inFiles>0)
	{
		FILE *mergedFile = fopen("merged.txt", "a");
		fdin = merger(mergedFile, Cmd -> inFiles, num_inFiles);
	}
	//********************************************************************

    //Implementing cd (change directory)
    SimpleCommand temp_scmd = scmd[0][0];
    if(strcmp(temp_scmd.args[0], "cd")==0){
        int status = ex_cd(temp_scmd.args[1]);
        getcwd(current_wd,MAX_PATH_LEN);
        if (status == -1){
			fprintf(stderr, "error: directory doesn't exist\n");
        }
        else if(status == 2){
            fprintf(stderr,"error: directory is not reachable\n");
        }
        return;
    }
    
    // Set up initial input
    if (num_inFiles) {
        //fdin = open(inFile, O_RDONLY);
        if (fdin < 0) {
            perror("open input file");
            exit(EXIT_FAILURE);
        }
    } else {
        // Use default input
        fdin = dup(tmpin);
    }

    int ret;
    int fdout;
    for (int i = 0; i < numsimplecommands; i++) {
        // Redirect input
        dup2(fdin, 0);
        close(fdin);

        // Setup output
        if (i == numsimplecommands - 1) {
            if (outFile) {
				if(out_append)
				{fdout = open(outFile, O_WRONLY | O_CREAT | O_APPEND, 0644);}
				else
                {fdout = open(outFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);}
                if (fdout < 0) {
                    perror("open output file");
                    exit(EXIT_FAILURE);
                }
            } else {
                fdout = dup(tmpout);
            }
        } else {
            int fdpipe[2];
            if (pipe(fdpipe) == -1) {
                perror("pipe");
                exit(EXIT_FAILURE);
            }
            fdout = fdpipe[1];
            fdin = fdpipe[0];
        }

        dup2(fdout, 1); // Redirect output
        close(fdout);

        ret = fork();
        if (ret == 0) {
            // Child process
            execvp(scmd[i] -> args[0], scmd[i] -> args);
            perror("execvp");
            _exit(EXIT_FAILURE);
        } else if (ret < 0) {
            perror("fork");
            exit(EXIT_FAILURE);
        }
    }

    dup2(tmpin, 0);
    dup2(tmpout, 1);
    close(tmpin);
    close(tmpout);

    if (!background) {
        waitpid(ret, NULL, 0);
    }
    
}

void freeCmd(Command *Cmd){
    if (!Cmd){
        return;
    }
    for (int i = 0; i < Cmd -> num_simCmds; i++) {
        free(Cmd -> simCmds[i] -> args);
        free(Cmd -> simCmds[i]);
    }
    free(Cmd -> simCmds);
    free(Cmd);
}

void freeSimCmd(SimpleCommand *simCmd){
    if (!simCmd){
        return;
    }
    free(simCmd -> args);
    free(simCmd);
}
