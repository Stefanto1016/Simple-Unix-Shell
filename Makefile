all: sshell

example: sshell.c
	gcc -Wall -Wextra -Werror -o sshell sshell.c

clean:
	rm -f sshell
