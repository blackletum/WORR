# Menu Navigation and Accessibility

WORR menus support mouse, keyboard, and gamepad navigation. The same controls
work in the main menu, settings, browsers, Player Setup, and multiplayer match
menus.

## Moving around

- Use the mouse or directional keys/gamepad controls to move focus.
- Press Enter, Space, or the gamepad confirm button to activate the focused
  control.
- Press Escape or the gamepad back button to return to the previous menu.
- The square back plate beside a page title performs the same action as
  Escape.
- On long pages, use the mouse wheel or the focused scroll area to reach more
  content.

Gold highlighting shows the current focus or selection. Green marks the main
action, orange identifies live-match information, and red is reserved for
actions such as Quit, Forfeit, or Leave Match.

## Accessibility settings

Open **Options → Accessibility**. Changes apply immediately.

- **High-Visibility Text** adds dark backing to HUD text and uses the TrueType
  face for clearer reading.
- **Typeface** selects Legacy, KEX, or TrueType text when High-Visibility Text
  is off.
- **Large Menu Text** increases menu text and control target sizes.
- **High-Contrast Menus** uses white-on-black menus with yellow focus
  highlights.
- **Reduce Motion** disables menu animations and hover fades.

These settings remain active when switching between menu pages and renderer
backends. Menu layout also scales for 4:3, widescreen, and ultrawide displays;
dense lists scroll instead of pushing important actions off-screen.

## Language

Open **Options → Language** to change the menu language. An open menu refreshes
when the language changes. If a translation is unavailable, WORR keeps a
readable English fallback rather than showing an empty label.

## Confirmation dialogs

Quit, Leave Match, Forfeit, and tournament replay actions use confirmation
dialogs. Focus starts on the safe choice. Review the message, then explicitly
choose the destructive action if you want to continue.
