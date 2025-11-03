# v2.0.2

- *massive blunder...*
- Fixed issue where downloading a custom music and leaving the level info screen causes the menu music to not play at all

# v2.0.1

- Fixed issue where the random offset isn't applied when the song finished downloading
- Fixed issue where on mobile, the preview music and the menu music playing at the same time
- Fixed issue where the preview music still plays while transitioning to the level
- Fixed issue where built-in track doesn't stop playing when exiting level info screen
- Fixed issue where music being deleted and exit the level info layer still restores the menu music position
- Changed the preview music FMOD channel to 0 for consistency

# v2.0.0

- **Refactored the entire codebase because I wanted to make this better and also it was kinda bad code**
- Better detection on where to play the preview music *(It will always play the preview music if you enter to the level info screen)*
- Better restore menu music position when exiting level info screen
- Background music automatically plays when deleting custom music
- Fixed issues where the level music doesn't play when you re-enter the level info screen from a different screen
- Removed delegates that are no longer needed
- Removed the PlayLayer hook since that is very unnecessary
- Removed stub code that wasn't really used correctly
- Preview Music will remember which custom music to play, even when switching levels
- Added option to play preview music at a random offset

# v1.1.0

- Fixed issues where the custom music is still playing when exiting level from the editor
- Fixed issues where game crashed randomly when a custom music downloaded finished
- Fixed issues with built-in music playing at the same time

# v1.0.9

- Fixed issues with the in built music being broken
- Fixed issues with some sound effects stops playing when exiting level

# v1.0.8

- Fixed issue with both custom music and main menu music playing simultaneously when exiting level
- Fixed issue with custom music plays at the very start of the level when exiting level

# v1.0.7

- Fixed crash when music finished downloading

# v1.0.6

- Fixed the inbuilt track not playing mid-song
- Fixed the main menu music not resuming after exiting the level info screen
- Main Menu music will not cut off when the custom music isn't downloaded yet
- Improved overall audio playback stability
- Fixed issue with custom music not playing after exiting level
- Fixed issues with other mods interferring with the FMOD channels, *yea i see you [Menu Loop Randomizer](mod:elnexreal.menuloop_randomizer)*

# v1.0.5

- Fixed the menu music not playing when exiting the level info screen
- Fixed the issues with the inbuilt track not playing

# v1.0.4

- Refactored the inbuilt track

# v1.0.3

- Very minor changes

# v1.0.2

- Minor changes
- Fixed **Preview Music mid-song** to work on MacOS
- Fixed issues with preview music still playing when starting the level

# v1.0.1

- Reworking the retry logic for built-in tracks
- Attempted to fix the audio playback issues for mobile users

# v1.0.0

- Initial release
