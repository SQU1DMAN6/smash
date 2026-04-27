# SMASH - Simple POSIX Shell

A lightweight, modern shell implementation written in C with real-time syntax highlighting, fish-like history search, and comprehensive scripting support.

## Features

- **Real-time Syntax Highlighting**: Commands in green (when valid) or red (when invalid), flags in blue, strings in orange, comments in grey
- **Fish-like History Search**: Prefix-based history navigation using Up/Down arrow keys
- **PS1 Customization**: Dynamic prompt with variable and command substitution
- **Configuration File**: `~/.smashrc` for shell initialization
- **History Persistence**: Command history saved to `~/.smash_history`
- **Piping and Redirection**: Full support for pipes, input/output redirection, and append
- **Rich Builtins**: Comprehensive set of shell commands for practical use

## Installation

```bash
cd smash
make
./smash
```

## Usage

### Interactive Mode

```bash
./smash
```

### Execute Command and Exit

```bash
./smash -c 'echo hello world'
```

### Run Script File

```bash
./smash script.sh
```

## Configuration

### PS1 Prompt Customization

Create `~/.smashrc` to customize your shell:

```bash
# Set custom prompt
export PS1='$(pwd)=>'

# Two-line prompt with user, hostname, and directory
export PS1='($(whoami)@$(hostname))[$(pwd)]\r\n=>'

# Simple prompt
export PS1='$ '

# Minimal prompt
export PS1='> '
```

The default PS1 is `$(pwd)=>`, which displays the current working directory followed by `=>`.

### Escape Sequences in PS1

- `\n` - Newline
- `\r` - Carriage return
- `$(command)` - Execute command and substitute output
- `$VARIABLE` - Substitute environment variable

## Builtins

### Navigation and Environment

#### `cd [directory]`
Change directory. Supports:
- `cd ~` or `cd` - Go to home directory
- `cd -` - Go to previous directory
- `cd path` - Change to specified path
- `cd ~/relative/path` - Tilde expansion

#### `pwd`
Print working directory.

#### `export [name=value ...]`
Export variables to child processes:
```bash
export USER_VAR=value
export VAR1=val1 VAR2=val2
```

#### `unset [name ...]`
Unset environment variables:
```bash
unset VAR1 VAR2
```

#### `set [name=value ...]`
Set local shell variables (not exported to child processes):
```bash
set LOCAL_VAR=value
```

#### `declare [name=value ...]`
Declare variables (similar to set):
```bash
declare USER_VAR=myvalue
```

### Command Management

#### `type [command ...]`
Show the type of command (builtin or external):
```bash
type echo
type pwd
type custom_cmd
```

#### `which [command ...]`
Locate a command in PATH:
```bash
which ls
which python
```

#### `alias [name=command ...]`
Create or list command aliases:
```bash
alias ll='ls -la'
alias la='ls -A'
alias  # List all aliases
alias ll  # Show specific alias
```

### History

#### `history`
Display command history:
```bash
history      # Show all commands
history -c   # Clear history
```

### File and System

#### `clear`
Clear the terminal screen.

#### `source [file]` or `. [file]`
Execute commands from a file:
```bash
source ~/.smashrc
. ./setup.sh
```

### Shell Control

#### `help`
Display list of available builtins.

#### `exit [code]`
Exit the shell with optional exit code:
```bash
exit          # Exit with code 0
exit 42       # Exit with code 42
```

## Scripting

### Basic Script Example

Create a file `myscript.sh`:

```bash
#!/usr/bin/env ./smash

# Variables
NAME="World"
GREETING="Hello"

# Echo with variable substitution
echo $GREETING $NAME

# Directory navigation
cd ~
pwd

# Piping
ls -la | grep smash

# Output redirection
echo "Log entry" > app.log
echo "Appended" >> app.log

# Command input
cat < input.txt
```

Run it:
```bash
./smash myscript.sh
```

### Redirection Operators

#### Output Redirection
```bash
echo "text" > file.txt      # Overwrite file
echo "text" >> file.txt     # Append to file
```

#### Input Redirection
```bash
cat < input.txt             # Read from file
```

#### Here-Document
```bash
cat << EOF
This is a multi-line
text block
EOF
```

### Piping

Connect commands together:

```bash
ls | grep pattern
cat file.txt | sort | uniq
ps aux | grep process_name
```

## Interactive Features

### History Navigation

- **Up Arrow**: Search backward through history by current prefix
- **Down Arrow**: Search forward through history by current prefix
- **Ctrl+Left/Right**: Jump to previous/next word
- **Ctrl+A**: Go to start of line
- **Ctrl+E**: Go to end of line
- **Ctrl+C**: Cancel current input
- **Ctrl+D**: Exit shell (if input is empty)

### Syntax Highlighting

Colors are displayed in real-time as you type:

- **Green**: Valid commands
- **Red**: Invalid/non-existent commands
- **Blue**: Command flags (starting with `-`)
- **Orange**: Quoted strings
- **Grey**: Comments (starting with `#`)
- **White**: Other text

## Environment Variables

### Special Variables

- `PWD` - Current working directory
- `HOME` - User's home directory
- `OLDPWD` - Previous working directory
- `PATH` - Executable search path
- `PS1` - Primary shell prompt
- `SMASH_SHELL` - Always set to "1" in SMASH

### Common Usage

```bash
# Display all environment variables
set

# Access variable
echo $HOME
echo $USER
echo $PATH

# Modify PATH
export PATH="$PATH:/new/path"
```

## Configuration Examples

### ~/.smashrc

```bash
# Set a nice two-line prompt
export PS1='($(whoami)@$(hostname))[$(pwd)]\r\n=>'

# Create useful aliases
alias ll='ls -la'
alias la='ls -A'
alias grep='grep --color=auto'
alias df='df -h'

# Set environment variables
export EDITOR=nano
export PAGER=less

# Create functions (note: SMASH doesn't support functions yet, use aliases instead)
alias mkcd='mkdir -p'
```

## Technical Details

### Path Expansion

The shell supports tilde (`~`) expansion:
- `~` expands to `$HOME`
- `~/path` expands to `$HOME/path`
- Works in command arguments and redirection targets

### Variable Expansion

Variables are expanded in double-quoted strings and command substitutions:
```bash
set VAR=value
echo $VAR              # Prints: value
echo "Value is $VAR"   # Prints: Value is value
```

### Command Substitution in Prompts

The PS1 prompt supports command substitution:
```bash
export PS1='$(pwd)=> '           # Current directory
export PS1='$(whoami)$ '         # Current user
export PS1='[$(date)]> '         # Current date/time
```

## Limitations

Currently, SMASH does not support:
- Logical operators (`&&`, `||`)
- Command grouping (`()`, `{}`)
- Job control (background/foreground jobs)
- Functions (use aliases instead)
- Arrays and associative arrays
- Advanced parameter expansion
- Pattern matching and globbing

These features may be added in future versions.

## Examples

### Create and run a simple script

```bash
cat > hello.sh << 'EOF'
#!/usr/bin/env ./smash
echo "Hello from SMASH!"
pwd
ls | head -5
EOF

./smash hello.sh
```

### Process log files

```bash
# Count lines in log
cat app.log | grep ERROR > errors.log

# Search and redirect
grep -i warning syslog >> warnings.txt
```

### Environment setup

In `~/.smashrc`:
```bash
export PROJECT_DIR=~/myproject
export DEBUG=1
alias goproject='cd $PROJECT_DIR'
```

Then use in shell:
```bash
goproject
echo $PROJECT_DIR
```

### Two-line prompt with colors

```bash
# Note: colors in prompt may require terminal support
export PS1='[$(whoami)@$(hostname)]\r\n$(pwd)=> '
```

## Building from Source

Requirements:
- GCC or Clang
- POSIX-compatible system (Linux, macOS, etc.)
- Make

```bash
cd smash
make
./smash

# Clean build artifacts
make clean
```

## File Structure

```
smash/
├── main.c                 # Entry point
├── include/smash/         # Header files
│   ├── shell.h
│   ├── executor.h
│   ├── parser.h
│   ├── line_editor.h
│   ├── history.h
│   ├── builtins.h
│   ├── state.h
│   ├── term.h
│   └── util.h
├── src/                   # Implementation files
│   ├── shell.c
│   ├── executor.c
│   ├── parser.c
│   ├── line_editor.c
│   ├── history.c
│   ├── builtins.c
│   ├── term.c
│   └── util.c
├── Makefile
└── README.md
```

## Development

To contribute or extend SMASH:

1. Read the source code structure
2. Add new builtins in `src/builtins.c`
3. Update `include/smash/builtins.h` if needed
4. Add tests with `printf ... | ./smash`
5. Ensure code compiles without warnings: `make`

## Author

Written by Quan Thai
