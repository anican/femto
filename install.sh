#!/bin/bash
STR="alias femto='cd ~/.femto/ && ./femto && cd -'"

if [[ "$OSTYPE" == "linux-gnu" ]]; then
    # Linux OS
    cd ~/
    echo $STR >> .bashrc
    source ~/.bashrc
    cd -
elif [[ "$OSTYPE" == "darwin"* ]]; then
    # Mac OSX
    cd ~/
    echo $STR >> .bash_profile
    source ~/.bash_aliases
    cd -
    echo "Congratulations, femto was successfully installed!"
elif [[ "$OSTYPE" == "cygwin" ]]; then
    # Unix-like Shell for Windows
    cd ~/
    echo $STR >> .bashrc
    source ~/.bashrc
    cd -
else
    # more stuff
    MESS="Error! Femto text editor is not supported on "
    SYS=$OSTYPE
    echo $MESS$SYS
fi

