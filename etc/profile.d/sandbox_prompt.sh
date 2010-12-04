# Put the current sandbox in the prompt.
if [[ -n "$(which sandbox-which)" && -n "$(sudo sandbox-which)" ]]
then
	PS1="[$(sudo sandbox-which)] $PS1"
else
	PS1="[BASE] $PS1"
fi
