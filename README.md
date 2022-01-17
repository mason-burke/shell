# 6-shell-2

The structure of my code remains the same as in Shell 1, overall.
Now, in my execution function, I also check for fg, bg, or jobs commands.
Additionally, in the fork() part of my code, I now have checks for fg/bg
process initiation, and handle that within the execution function.
Finally, I now have a reaping function that I call to wait for all background
processes to terminate.

No bugs or extra features.

To compile, navigate to the proper directory, run make clean all, and then
type into the terminal "./33sh" for a shell with a prompt, and "./33noprompt"
for a shell without a prompt. From there, execute any command. To exit the shell,
either type CTRL + D or CTRL + \.
