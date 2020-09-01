zterm is intended to be a Linux console replacement.

As such, it behaves somewhat differently than most X11/Wayland terminal emulators.

For example, while under the hood it has 'tabs', the intended way to access them is by key binding.

So in my default config, alt-F1 to alt-F12 access terminals 1 through 12, win-F1 through win-F12 access terminals 13 through 24, shift-F1 through shift-F12 access terminals 25 through 36, and control-F1 through control-F12 access terminals 37 through 48.

These terminals are opened on access, and are closed when the underlying terminal (such as bash) exits, and while this causes the 'tabs' to be reordered, this doesn't change what terminal you get when you access it via a key binding.

As such, the tab bar is pretty useless, so while it can be enabled (right click on zterm to toggle the tab bar), it is disabled by default and isn't really recommended.

You can define different color maps in the config as well, such as grey on black, and black on white, and toggle through the right click menu, and you can move specific terminals to a separate window.  Those stay in the same process and you can switch between windows just by selecting a terminal via key binding that is in the other window.

(Both of these were added at a time when I needed to be able to show things on a projector.  Together they quite well for that job.)

Speaking of the config, the 'config' in the repository has my current config file, it needs to go in ~/.zterm/

A number of other options exist, please see the config file for details.

I don't really have any objections to adding features, though pull requests are preferred.
