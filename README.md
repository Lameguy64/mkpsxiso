# MKPSXISO
Basically a modern clone of BUILDCD that came included with the PlayStation Programmer's Tool SDK for generating ISO disc images used for developing PlayStation software (usually homebrew nowadays). The problem with BUILDCD however is that it's an old real-mode DOS program and will not work on 64-bit versions of Windows without a DOS emulator. Also, BUILDCD only produces CD images in an unusual image format used by early CD writers which must be converted to a usable ISO format using a separate tool, making the already slow ISO generation process even slower.

While other ISO generator tools such as the popular MKISOFS may work, most do not let you control the order of the files by LBA (which is very important when optimizing file orders to speed up file access times) and all do not support mixed-mode XA file integration for streaming files such as XA audio and MDEC STR video... Thus, MKPSXISO was made to replace BUILDCD to aid PlayStation homebrew development on modern systems.

MKPSXISO more or less replicates BUILDCD's functionality but better! The most notable difference is that MKPSXISO produces ISO images in either iso or cue+bin format so that generated images can immediately be loaded to an emulator or burned to a CD. The cue+bin format allows images to contain more than a single track usually for iso image projects that include CD-DA audio tracks.

Another notable difference is that MKPSXISO injects the Sony license data correctly into the disc image. However, the license data is not provided with the program so one must have a copy of the official PlayStation Programmer's Tool SDK (which can be found in www.psxdev.net) for the license data. This is to avoid possible legal problems when including Sony's license data into open source programs and it's better to be safe than sorry... Besides, there already exists disc patcher tools to inject license data if you don't use the PlayStation Programmer's Tool SDK anyway.

## Features
* Outputs in iso or cue+bin image format.
* Injects license data correctly.
* Manual file ordering to allow for file seek optimization.
* Supports mixed-mode XA file integration for CD-XA streams such as XA audio and STR video.
* Supports injecting CD-DA audio tracks into the generated ISO image.
* Uses XML for iso image project scripting.

## Binary Download
The latest precompiled Win32 binary of this program can be downloaded here:
[mkpsxiso-1.06.zip](http://lameguy64.github.io/mkpsxiso/mkpsxiso-1.06.zip)

Older versions:
[mkpsxiso-1.04.zip](http://lameguy64.github.io/mkpsxiso/mkpsxiso-1.04.zip)
[mkpsxiso-1.00.zip](http://lameguy64.github.io/mkpsxiso/mkpsxiso-1.00.zip)

## Compiling
This tool requires tinyxml2 to compile.

### Windows (CodeBlocks IDE without CMake)
1. Install CodeBlocks (Preferably with MinGW32 GCC compiler bundled).
2. Extract and compile tinyxml2 in the root of your C: drive (C:\tinyxml2).
3. Open the project file mkpsxiso.cbp inside the src directory.
4. Press Ctrl+F9 to compile the program.
5. The result will be in the base folder named "mkpsxiso.exe"

### Windows (CMake)
1. Install cygwin64 with the following:
  * make
  * cmake
  * gcc
  * tinyxml2
2. Open the cygwin64 terminal.
3. Navigate to the download of this repo.
4. Run "cmake ." to generate the make file.
5. Run "make" to compile the program.
6. The result will be in bin_win, named "mkpsxiso.exe"

### Linux (Ubuntu)
1. Install the following:
  * Build Essentials
  * cmake
  * tinyxml2
2. Open a terminal.
3. Navigate to the download of this repo.
4. Run "cmake ." to generate the make file.
5. Run "make" to compile the program.
6. The result will be in bin_nix, named "mkpsxiso"

## Changelog
**Version 1.06**
* Added file overwrite confirmation.
* Element and attribute strings are no longer case sensitive.
* File name strings from arguments are no longer passed as duplicated strings (eliminates the risk of forgetting to dealloc said strings on exit).
* Added -lba option to produce a complete listing of files and directories in the ISO file system with their LBA addresses.
* Added <dummy> element example in example.xml (forgot to put it in since the first release).

**Version 1.05**
* Fixed types for linux build, changed u_char and such to unsigned char.
* In cygwin64 version of tinyxml2, "XML_NO_ERROR" is not defined, changed with "XML_SUCCESS" and this works on both Windows and Linux.
* Converted to cmake, if you want a codeblocks project file, just run "cmake . -G "CodeBlocks - Unix Makefiles"" to create it.

**Version 1.04**
* Fixed a bug where you'll get a 'Data track must only be on first track' error when creating an ISO image with more than 2 tracks even when the extra tracks are CD audio.
* Duplicate file and directory entries are no longer possible to add (it'll result to weird problems anyway).

**Version 1.00**
* Initial release.
