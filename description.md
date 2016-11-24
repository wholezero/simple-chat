Simple Chat is an incredibly simple chat application. It's intended more as a proof-of-concept than anything else—no effort is made to support older browsers, there's no formatting, and it doesn't even enforce unique nicknames.

Simple Chat has two modes of operation: logged and off-the-record. In logged mode, history is stored to disk and replayed on startup, and a transcript of all chats ever can be retrieved by downloading a grain backup. In off-the-record mode, history is only stored in memory, only lasts as long as the grain is up, and can be cleared by anyone at any time.

Off-the-record mode may or may not be suitable for privacy-conscious users. No written record is kept, and if the server is configured to use https then messages can't be intercepted by third parties, but the Sandstorm instance itself may intercept and store messages.
