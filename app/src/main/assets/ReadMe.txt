DESCRIPTION

This project ports the Windows application Emu28 written in C to Android.
It uses the Android NDK. The former Emu28 source code (written by Christoph Giesselink) remains untouched because of a thin win32 emulation layer above Linux/NDK!
This win32 layer will allow to easily update from the original Emu28 source code.
It can open or save the exact same state files (state.e28) than the original Windows application!


Some KML files with theirs faceplates are embedded in the application but it is still possible to open a KML file and its dependencies by selecting a folder on your Android file system.
If you want to modify them, you can download the already embedded KML scripts here: http://regis.cosnier.free.fr/soft/androidEmu28/Emu28-KML-original-134.zip
Or you can download the KML scripts from the original Emu28 Windows application archive (https://hp.giesselink.com/emu28.htm) and add your ROM files.
Be careful about the case sensitivity of the filename in the KML script (Linux/Android is case sensitive, not Windows).

The application does not request any permission (because it opens the files or the KML folders using the content:// scheme).

The application is distributed with the same license under GPL and you can find the source code here:
https://github.com/dgis/emu28android


QUICK START

1. Click on the 3 dots button at the top left (or from the left side, slide your finger to open the menu).
2. Touch the "New..." menu item.
3. Select a default calculator (or "[Select a Custom KML script folder...]" where you have copied the KML scripts and ROM files (Android 11 may not be able to use the Download folder)).
4. And the calculator should now be opened.


NOTES

- For technical reason, this application need the Android 5.0 (API 21).
- The Help menu displays Emu28's original help HTML page and may not accurately reflect the behavior of this Android version.
- When using a custom KML script by selecting a folder (Not the folder Download for Android 11), you must take care of the case sensitivity of its dependency files.
- To speed up printing, set the 'delay' to 0 in the calculator's print options.


NOT WORKING YET

- Disassembler
- Debugger


LINKS

- Original Emu28 for Windows from Christoph Giesselink and Sébastien Carlier: https://hp.giesselink.com/emu28.htm
- The Museum of HP Calculators Forum: https://www.hpmuseum.org/forum/thread-12540.html


CHANGES

Version 1.6 (2024-10-29)

- Update from the original source code Emu28 version 1.38 from Christoph Gießelink.


Version 1.5 (2024-06-16)

- Update from the original source code Emu28 version 1.37 from Christoph Gießelink.
- Fix a background issue for annunciators in the Win32 layer.
- Fix haptic feedback with Android 12 (API deprecation).


Version 1.4 (2023-09-01)

- Update Android API.


Version 1.3 (2022-03-31)

- Update from the original source code Emu28 version 1.36 from Christoph Gießelink.
- Replaces the haptic feedback switch with a slider to adjust the vibration duration.
- Fix a timer issue which prevented to turn the calculator on after a switch off.
- Fix transparency issue (RGB -> BGR).
- Fix a printer issue from Christoph Gießelink's HP82240B Printer Simulator version 1.12.
- Fix the KML button Type 3 with a Background offset which was not display at the right location. But Type 3 does not work very well with Emu42.
- Fix an issue which prevents to save all the settings (Save in onPause instead of onStop).
- The KML folder is now well saved when changing the KML script for a custom one via the menu "Change KML Script...".
- Fix an issue when the permission to read the KML folder has been lost.
- Allows pressing a calculator button with the right button of the mouse but prevents its release to allow the On+A+F key combination (with Android version >= 5.0).
- Open an external web browser when you click an external links in the Help.
- Show KML log on request.
- Fix the upside down background of the LCD screen on high contrast (actually, fix a general top-down issue in the bitmap).
- Add the KML scripts and the calculator images in the application.
- Remove unneeded code.
- Display the graphic tab of the printer without antialiasing.
- Fix a crash about the Most Recently Used state files.
- Fix an issue with "Copy Screen".
- Allow to load RLE4, RLE8 and monochrome BMP images.
- Optimize the number of draw calls when displaying the LCD pixel borders.


Version 1.2 (2020-09-07)

- Update from the original source code Emu28 version 1.34 from Christoph Gießelink (which can read state file with KML url longer than 256 byte).
- If the KML folder does not exist (like the first time), prompt the user to choose a new KML folder.
- Move the KML folder in the JSON settings embedded in the state file because Windows cannot open the state file with KML url longer than 256 byte.
- Prevent to auto save before launching the "Open...", "Save As...", "Load Object...", "Save Object...", etc...
- Prevent app not responding (ANR) in NativeLib.buttonUp().
- Use the extension *.e28c to avoid confusion with the HP28S.
- In the menu header, switch the pixel format RGB to BGR when an icon of type BMP is defined in the KML script.


Version 1.1 (2020-05-24)

- Intercept the ESC keyboard key to allow the use of the BACK soft key.
- Add LCD pixel borders.
- Add support for the dark theme.
- Remove the non loadable file from the MRU file list.
- Fix: Overlapping window source position when Background/Offset is not (0,0).
- Wrap the table of content in the former help documentation.
- Save the settings at the end of the state file.
- Transform all child activities with dialog fragments (to prevent unwanted state save).
- Fix an issue with the numpad keys which send the arrow keys and the numbers at the same time.
- Fix a major issue which prevented to open a state file (with a custom KML script) with Android 10.
- Optimize the speed with -Ofast option.


Version 1.0 (2019-12-12)

- First public version available. It is based on Emu28 version 1.33 from Christoph Gießelink.
- It should have all the feature of Emu42 for Android version 1.2.


LICENSES

Android version by Régis COSNIER.
This program is based on Emu28 for Windows version, copyrighted by Christoph Gießelink & Sébastien Carlier.

This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.
This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
You should have received a copy of the GNU General Public License along with this program; if not, write to the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
