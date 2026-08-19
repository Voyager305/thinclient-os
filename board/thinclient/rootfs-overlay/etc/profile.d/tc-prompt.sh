# Приглашение user@host:cwd для любого логин-шелла.
# Выполняется после скелетного /etc/profile (тот ставит PS1='# ') и перебивает его.
if [ -n "$BASH_VERSION" ]; then
    PS1='\[\e[1;36m\]\u@\h\[\e[0m\]:\[\e[1;34m\]\w\[\e[0m\]\$ '
else
    PS1="$(whoami)@$(hostname): "
fi
export PS1
