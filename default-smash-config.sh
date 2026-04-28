# SMASH Configuration File (~/.smashrc)
# This file is sourced on shell startup

# ===== Prompt Configuration =====
# Simple one-line prompt showing directory
export PS1='$(pwd)=> '

# Alternative: Two-line prompt with user info
# export PS1='($(whoami)@$(hostname))[$(pwd)]\r\n=> '

# ===== Useful Aliases =====
alias ll='ls -lah'
alias la='ls -A'
alias l='ls -1'
alias cp='cp -i'
alias mv='mv -i'
alias rm='rm -i'
alias mkdir='mkdir -p'
alias cd..='cd ..'
alias ..='cd ..'
alias ls='ls --color=auto'
alias grep='grep --color=auto'

# ===== Development Aliases =====
alias gs='git status'
alias ga='git add'
alias gc='git commit'
alias gp='git push'

# ===== Environment Variables =====
export EDITOR=nano
export PAGER=less

# ===== Path Setup =====
# Uncomment to add custom directories to PATH
# export PATH="$PATH:$HOME/.local/bin"

# ===== Welcome Message =====
echo "Welcome to SMASH Shell"