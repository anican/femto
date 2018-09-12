# femto
An extensible text editor in the style of vim and nano
femto is supported on MacOS, Cygwin, and Linux distributions

Windows support forthcoming... I still need to learn the basics of batch scripting

## Introduction
After adopting vim (more recently, neovim) as my primary text editor, I became increasingly fascinated with how
text editors work, and this project is my attempt to learn more about the C programming
language and text editor internals. Text-editor technology is super old but still extremely
useful. I figure it couldn't hurt to know to build one.

## Installation
Installation is super easy. Just follow the steps below:
`git clone https://github.com/anican/femto.git && cp femto/femto ~/.femto/femto && cd -`
Once you've done that, cd into femto and run the bash installation script:
`./install.sh`
After this, you can delete the cloned repository if you so choose

(note: I might need to add a script that saves the installation and Uninstallation
processes to the local machine)

If you are unable to run the installation script, enter the following into your terminal:
`chmod +x install.sh`

### Uninstallation
Forthcoming... 

## Configuring Femto
CTRL-S to save
CTRL-Q to quit

## TODO
- organize files into respective /bin and /src files
- split up GIGANTIC src file into multiple smaller files for readability
- edit Makefile, install.sh, and README.md to reflect reallocation int /bin and /src
- add uninstall.sh
- add clean feature to Makefile
