#include <cstdlib>
#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <sys/wait.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <regex>
#include <queue>
#define EOC ""

static bool invalid_command(const std::vector<std::string>& tokens_subset) {
	size_t size_with_terminator = tokens_subset.size();
	unsigned short nlt = 0, ngt = 0, npo = 0;
	std::string next_tok;

	for(size_t i = 0; i < size_with_terminator; i++) {
		if(tokens_subset[i] == "<") {
			nlt++;

			next_tok = tokens_subset[i + 1];
			if(next_tok == "<" || next_tok == ">" ||
			   next_tok == "|" || next_tok == EOC) {
				return true;
			}
		}
		else if(tokens_subset[i] == ">") {
			ngt++;

			next_tok = tokens_subset[i + 1];
			if(next_tok == "<" || next_tok == ">" ||
			   next_tok == "|" || next_tok == EOC) {
				return true;
			}
		}
		else if(tokens_subset[i] == "|") 
			npo++;
	}

	if(nlt > 1 || ngt > 1) return true;

	if(nlt + ngt == size_with_terminator - nlt - ngt - npo - 1) 
		return true;	// number of words == number of redirection operators

	return false;
}

class Cmd {
public: 
    std::string redirect_stdout;
    std::string redirect_stdin;
    bool needs_redout = false;
    bool needs_redin = false;
    bool is_invalid = false;
    std::vector<std::string> argv;

    Cmd(std::vector<std::string> tokens_subset) {
        std::string tok;
        int gt_ind = 0, lt_ind = 0;
        
        if(invalid_command(tokens_subset)) {
        	std::cerr << "You have entered an invalid command.\n";
        	is_invalid = true;
        	return;
        }
        
        for(size_t i = 0; i < tokens_subset.size() - 1; i++) {
            tok = tokens_subset[i];
            if(tok == ">") {
            	needs_redout = true;
                redirect_stdout = tokens_subset[i + 1];
                gt_ind = i;
            }
            if(tok == "<") {
            	needs_redin = true;
                redirect_stdin = tokens_subset[i + 1];
                lt_ind = i;
            }
        }

        auto it = tokens_subset.begin();
        if(needs_redout) {
            tokens_subset.erase(it + gt_ind, it + gt_ind + 2);
        }
        if(needs_redin) {
            if(needs_redout && gt_ind < lt_ind) lt_ind -= 2;
            it = tokens_subset.begin(); tokens_subset.erase(it + lt_ind, it + lt_ind + 2);
        }

        argv = tokens_subset;
    }

};  


void interpret_status(std::string child_name, int status) {
    if(WIFSIGNALED(status)) 
        std::cout << child_name << " terminated with signal " << WTERMSIG(status) << std::endl;
    else std::cout << child_name << " exit status: " << WEXITSTATUS(status) << std::endl;
}


void Exec(Cmd cobj) {
	size_t argc = cobj.argv.size();
	char **argv_exarg = new char*[argc];

	for(size_t i = 0; i < argc; i++) 
		argv_exarg[i] = (char*) cobj.argv[i].c_str();
	argv_exarg[argc - 1] = NULL;
	
    execv(argv_exarg[0], argv_exarg);

    delete [] argv_exarg;
    perror("execv");
    exit(errno);
}


void parse_and_run_command(const std::string &command) {
    if (std::regex_match(command, std::regex("\\s*exit\\s*")) ||
    	std::regex_match(command, std::regex("\\s*")))
      exit(0);

    std::istringstream s(command); 
    std::string tok;
    std::vector<std::string> tokens;
    std::vector<Cmd> pipeline;
    std::queue<std::string> child_names; // how parent can print out what the child's name is
    std::vector<std::string> v;
    std::vector<pid_t> pids;
    int status;

    while(s >> tok) 
        tokens.push_back(tok);
    tokens.push_back(EOC);
    
    auto beg = tokens.begin();
    auto b = tokens.begin(); 
    for(size_t i = 0; i < tokens.size(); i++) {
        if(tokens[i] == "|" || tokens[i] == EOC) {
        	tokens[i] = EOC;
            std::vector<std::string> v(b, beg + i + 1);
            Cmd c(v);
            if(c.is_invalid) return;

            pipeline.push_back(c);   
            child_names.push(c.argv[0]);

            b = beg + i + 1;
        }
    }  
    
    // instantiate the pipes, of which there are (# commands - 1)
    int ncom = pipeline.size();   
    int *procchans = new int[2*(ncom - 1)];  // will need to be dellocated upon syscall fail
    for(int i = 0; i < ncom - 1; i++) {
        if(pipe(procchans + i*2) < 0) {
        	delete [] procchans;
        	perror("pipe");
        	return;        
        }
    }
    

    for(int i = 0; i < ncom; i++) {  
        Cmd cobj = pipeline[i];
        pid_t child_pid = fork();

        if(child_pid < 0) {
        	delete [] procchans;
        	std::cerr << "Fork error...\n";
        	return;
        }
        else if(child_pid > 0) { 
            pids.push_back(child_pid);
        }
        else if(child_pid == 0) { // child 
            if(i < ncom - 1 /* this command is not the last command in the sequence */) {
                dup2(procchans[i*2 + 1], STDOUT_FILENO); 
            }
            if(i > 0 /* this command is not the first command in the sequence */) {
                dup2(procchans[i*2 - 2], STDIN_FILENO);
            }
            if(cobj.needs_redout) {
                int dst_hndl = open(cobj.redirect_stdout.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666); 
                if(dst_hndl < 0) {
                	delete [] procchans;
                	perror("open");
                	exit(errno);
                }
                dup2(dst_hndl, STDOUT_FILENO); close(dst_hndl);
            }
            if(cobj.needs_redin) {
                int src_hndl = open(cobj.redirect_stdin.c_str(), O_RDONLY, 0666);
                if(src_hndl < 0) {
                	delete[] procchans;
                	perror("open");
                	exit(errno);
                }
                dup2(src_hndl, STDIN_FILENO); close(src_hndl);
            }

            // CLOSE ALL PIPES IN THIS CHILD
            for(int i = 0; i < 2*(ncom - 1); i++) close(procchans[i]);
            delete [] procchans;

            Exec(cobj);
        }
    }

    // CLOSE ALL PIPES IN THE PARENT
    for(int i = 0; i < 2*(ncom - 1); i++) close(procchans[i]);
    delete [] procchans;
	
    for(pid_t child_pid : pids) {
        waitpid(child_pid, &status, 0);
        interpret_status(child_names.front(), status);
        child_names.pop();
    }    

}


int main(void) {
    std::string command;
    std::cout << "> ";
    while (std::getline(std::cin, command)) {
        parse_and_run_command(command);
        std::cout << "> ";
    } 
    return 0;
}