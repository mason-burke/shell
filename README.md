# shell

## overview

This project was completed for my Fall 2021 course "Introduction to Computer Systems." The objective was to create a shell capable of functioning just as a linux shell would. Here is the list of available builtin commands:

    cd
    rm
    ln
    exit
    jobs
    fg
    bg
    
Additionally, this shell features user-defined executable support, I/O redirection, proper whitespace handling, signal handling, and reaping of child processes.

## disclaimers

All of sh.c was written by me, as well as the Makefile. jobs.c and jobs.h were included as template code, to enable us to focus on the shell without also implementing a system to keep track of jobs.

## how to use

To compile, navigate to the proper directory, run 

    make clean all

and then run
    
    ./33sh

for a shell with a prompt

    ./33noprompt
    
for a shell without a prompt. From there, execute any of the supported commands. To exit the shell, type either CTRL + D or CTRL + \.
