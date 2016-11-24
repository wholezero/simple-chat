# v0.6.0

* New: Reset history in OTR chats.
* New: UI tweaked; now there's only one text box and that box is refocused
  whenever you send a chat.
* Other: Remove requireCanonicalPath, which isn't needed since we're doing
  simple string comparison on paths in all cases.

# v0.5.0

* New: Off-the-record mode.
* Fix: Don't write joins or parts to logs anymore.
* Other: Refer to grains as rooms instead of instances.

# v0.4.0

* Fix: Don't clear the line until the message is successfully sent.
* Other: Streamline the js a little.

# v0.3.5

* Other: Don't do a separate write for the newline.

# v0.3.4

* Other: Store chat stream in a vector so it isn't reallocated with each new chat.

# v0.3.3

* Other: Provide a blank cpuinfo.
* Other: Fill out more of the pkgdef.
* Other: Change github project to wholezero/simple-chat.

# v0.3.2

* Initial release. User pane, topic setting, chat window.
