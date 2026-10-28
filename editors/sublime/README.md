# Sublime Text: Sly

Highlighting for slight's C-style syntax, `.sly` files (D246,
`docs/SYNTAX.md`): keywords, the `fn` it defines (for Goto Symbol),
parameters, calls (the process builtins apart), `:keywords`, strings
and their escapes, numbers, operators, `self` and `parent`, `@ARGV`
and `%ENV`, and doc blocks, whose Markdown is highlighted as Markdown.
`Ctrl+/` (`Cmd+/`) comments lines out with `//`.

To install, link the `Sly` folder into Sublime's Packages folder
(Preferences > Browse Packages... shows where it is):

    # macOS
    ln -s "$PWD/editors/sublime/Sly" ~/Library/Application\ Support/Sublime\ Text/Packages/Sly
    # Linux
    ln -s "$PWD/editors/sublime/Sly" ~/.config/sublime-text/Packages/Sly

A `.sly` file then opens as Sly. Being a link, it follows the repository.

`syntax_test_sly.sly` is Sublime's own kind of test: open it in Sublime
and build (`Ctrl+B`, "Syntax Tests"). Each `// ^^^ scope` line says what
the columns it points at must be in the line above it. When the reader
(`compiler/src/sly.ts`) gains a word or an operator, the syntax and its
test should too.
