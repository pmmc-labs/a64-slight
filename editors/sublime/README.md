# Sublime Text: Slight

`Slight/` is a Sublime Text package with two syntaxes, which scope alike
so that they look alike in any colour scheme:

- **Slight**, for `.slight` files, the s-expressions: the function a
  `defun` defines (for Goto Symbol) and its parameters, special forms by
  kind (conditionals, `recv`, the process forms, `let`, `quote`,
  `@include`), the process builtins apart from other calls, `:keywords`
  and quoted symbols, numbers as the reader takes them (`1e3` is a name),
  strings and their escapes, `#true`, `#false`, `$$`, `^$$`, `@ARGV`,
  `%ENV`, and a stray backquote or comma as the error it is.
- **Sly**, for `.sly` files, the C-style syntax (D246,
  `docs/SYNTAX.md`): the same, as that syntax writes it, and its
  operators by kind.

Both highlight a doc block's Markdown as Markdown, and `Ctrl+/` (`Cmd+/`)
comments lines out with `;` or `//`.

To install, link the folder into Sublime's Packages folder (Preferences
> Browse Packages... shows where it is):

    # macOS
    ln -s "$PWD/editors/sublime/Slight" ~/Library/Application\ Support/Sublime\ Text/Packages/Slight
    # Linux
    ln -s "$PWD/editors/sublime/Slight" ~/.config/sublime-text/Packages/Slight

Being a link, it follows the repository.

`syntax_test_slight.slight` and `syntax_test_sly.sly` are Sublime's own
kind of test: open one and build (`Ctrl+B`, "Syntax Tests"). Each
assertion line, `; ^^^ scope` or `// ^^^ scope`, says what the columns
it points at must be in the line above it. When the language gains a
special form or a builtin worth showing, or the `.sly` reader a word or
an operator, the syntaxes and their tests should too.
