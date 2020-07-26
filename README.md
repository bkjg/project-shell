# Project shell
Shell project for Operating Systems course

Shell supports:
- bang operator (execute last command)
- save history to file and have possibility for displaying it
- instead of displaying just # as a prompt, display current working directory CWD
- expand file name patterns (for redirections, the first matching argument is chosen)
- support pipes, signals, redirects, running background processes (also supports bg and fg functions)
- prompts are displayed using readline and commands are also loaded from there
- commands are run in the following way: first, it is checked if a given command belongs to the built-in ones,
  if not, the command name is appended to each path from the $ PATH variable one by one, until the command is successful

