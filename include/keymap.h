/*
 * keymap.h — configurable A-Z matrix-switch bindings for Encore (unicorn).
 *
 * Ported from main/QEMU's qemu/p2k-switch-keymap.c. Lets the operator bind
 * letter keys A-Z to arbitrary Pinball 2000 matrix switches (11..88) via a
 * strict YAML-subset file, instead of the hardcoded F-key/digit layout.
 */
#ifndef ENCORE_KEYMAP_H
#define ENCORE_KEYMAP_H

#include <stdbool.h>
#include <SDL2/SDL.h>

/* Load bindings from `path` (NULL → default ~/.config/encore/switch-keymap.yaml,
 * created with a starter file if absent). Returns number of bindings loaded.
 * On any malformed line the whole file is rejected (returns 0) — matching
 * main's fail-closed behavior. Safe to call once at startup. */
unsigned keymap_install(const char *path);

/* Feed an SDL key event through the keymap. Returns true if the key was a
 * bound letter (and switch state was updated), false if unmapped so the
 * caller can fall through to its normal handling. */
bool keymap_handle_key(SDL_Keycode sym, bool down);

/* True if any bindings are active (keymap_install loaded >0). */
bool keymap_active(void);

#endif /* ENCORE_KEYMAP_H */
