
# crish shell

Custom **macOS** shell made with ❤️ syntax highlighting and tab completion.

> If you want me to add a specific feature raise an issue.

It supports:

- Command execution
- Operators `;`, `&`, `|`, `<<`, `<<<`, `>>`, `>>>`
- File completion with `TAB`
- Command completion with `TAB`
- Syntax highlighting
- History navigation with UP and DOWN arrows
- History persistence into `~/.crish_history`
- Path editing via `~/.crish.json` configuration file
- Custom prompt via `~/.crish.json` configuration file

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

## Configuration

Create a `crish.json` file in your home directory (it can also be marked hidden).
> `"symlinks"` and `"executables"` haven't been implemented yet.

```json

{
	"theme": {
		"prompt":  { "fg": "#00ff00" },
		"failure": { "fg": "#ff0000" },
		"success": { "fg": "#00ff00" },
		"strings": { "fg": "#0aad98" },
		"numbers": { "fg": "#00ffff" },
		"arguments": { "fg": "#ffff00" },
		"paths": { "fg": "#c966c9" },
		"files": { "fg": "#fff" },
		"folders": { "fg": "#fff", "bg": "#0085db" },
		"hidden-files": { "fg": "#999" },
		"hidden-folders": { "fg": "#fff", "bg": "#555" },
		"executables": { "fg": "#00fa00" },
		"symlinks": { "fg": "#fff" }
	},
	"prompt": {
		"symbol": "%",
		"components": [
			{ "text": "", "bold": true },
			{ "text": "$HH:$MM:$SS", "fg": "#fe5f00", "bold": true },
			"with",
			{ "text": "$USER", "fg": "#fe5fd7", "italic": true },
			"in",
			{ "text": "$CWD", "fg": "#36f6f7", "underline": true },
			"on",
			{ "text": "$KERNEL", "fg": "#1893ff", "bold": true },
			{ "text": "took", "enabler": "$DURATION" },
			{ "text": "$DURATION", "fg": "#f5f543", "enabler": "$DURATION" }
		]
	},
	"path": [
		"/bin",
		"/sbin",
		"/usr/bin",
		"/usr/sbin"
	]
}
```
