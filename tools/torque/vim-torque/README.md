# Torque syntax support for vim

This plugin adds rudimentary syntax highlighting support for the Torque
domain-specific language used in V8.

## Installation

Installation depends on your favorite plugin manager.

**Pathogen:**

Run

```sh
ln -s $V8/tools/torque/vim-torque ~/.vim/bundle/vim-torque
# or ~/.config/nvim/bundle/vim-torque for Neovim
```

**Vundle:**

Add this line to your `.vimrc` or `~/.config/nvim/init.vim`.

```vim
Plugin 'file:///path/to/v8/tools/torque/vim-torque'
```

**vim-plug:**

Add this line to your `.vimrc` or `~/.config/nvim/init.vim`.

```vim
Plug '~/path/to/v8/tools/torque/vim-torque'
```
