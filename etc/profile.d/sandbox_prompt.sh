# Put the current sandbox in the prompt.
which sandbox-which >/dev/null && {
	PS1="[$(sudo sandbox-which)] $PS1"
}
