
# crish shell

Custom shell made with ❤️ and syntax highlighting.

It supports:

- Command execution
- Operators `;`, `&`, `|`, `<<`, `<<<`, `>>`, `>>>`
- File completion with `TAB`
- Command completion with `TAB`
- Syntax highlighting
- History navigation with UP and DOWN arrows
- History persistence into `~/.crish_history`

### Compilation

```sh
clang++ --std=c++20 -o crish crish.cpp
```

### Login Shell Change

To change the default shell to crish, run the following commands:

```sh
echo /path/to/crish | sudo tee -a /etc/shells
chsh -s /path/to/crish
```

If on startup you see the `Last login: ... on ttys003` message,
you can silence it by running `touch ~/.hushlogin`.
