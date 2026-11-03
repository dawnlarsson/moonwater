/*
        The words the system's own windows say, in English: one line for each, its id and what it says.
        English is the table's index and costs nothing to ask; every other language is a file under
        languages/ (ui/language.c says how it is read), made of the same ids.

        Kept sorted by id, which the lookup of a file's lines relies on, and an id is used by code as
        WORDS(id). An id says what the words are for (menu_copy, ui_field_full), never what they say, so
        a change of wording leaves the id; and its text is whole, a sentence or a label with its
        placeholders (%s, %d, or %1$s to be placed again), because words made of pieces cannot be
        put in another language's order. A mnemonic is an & before one ASCII letter.

        A text that code compares is not here: it is a key, and stays where it is compared (registry.c
        keeps the Settings rows' labels and choice words, which are shown through language_of).
*/
LANGUAGE_ID(action_copy, "Copy")
LANGUAGE_ID(action_cut, "Cut")
LANGUAGE_ID(action_paste, "Paste")
LANGUAGE_ID(action_select_all, "Select All")
LANGUAGE_ID(menu_copy, "&Copy")
LANGUAGE_ID(menu_cut, "Cu&t")
LANGUAGE_ID(menu_delete, "&Delete")
LANGUAGE_ID(menu_paste, "&Paste")
LANGUAGE_ID(menu_redo, "&Redo")
LANGUAGE_ID(menu_select_all, "Select &All")
LANGUAGE_ID(menu_undo, "&Undo")
LANGUAGE_ID(ui_confirm_again, "press again to confirm: ")
LANGUAGE_ID(ui_field_digits, "a number is digits")
LANGUAGE_ID(ui_field_full, "that is as much as it holds")
