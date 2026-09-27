# Editors

LiteForth stores source code in blocks. Existing block editors are slim pickings.
One will need to be written for LiteForth. Before that, let's look at how developers edit.

There are two types of editors: modal and non-modal. Vim and Neovim are modal.
Visual Studio Code, Notepad++, and Nano are non-modal.

According to the Stack Overflow Developer Survey, developer usage of various editors are:

- Visual Studio Code: ~76%
- Notepad++: ~27%
- Vim: ~24%
- Neovim: ~14%
- Nano: ~12%

That breaks down into 1/3 modal and 2/3 non-modal editor uptake.
You can't make the non-modal people go modal.

The best compromise is to provide the classic Forth line editor, which is modal,
and a non-modal screen editor that anyone can use.

