# MKPSXISO
Basically a modern clone of BUILDCD that came included with the PlayStation Programmer's Tool SDK for generating ISO disc images used for developing PlayStation software (usually homebrew nowadays). The problem with BUILDCD however is that it's an old real-mode DOS program and will not work on 64-bit versions of Windows without a DOS emulator. Also, BUILDCD only produces CD images in an unusual image format used by early CD writers which must be converted to a usable ISO format using a separate tool, making the already slow ISO generation process even slower.

While other ISO generator tools such as the popular MKISOFS may work, most do not let you control the order of the files by LBA (which is very important when optimizing file orders to speed up file access times) and all do not support mixed-mode XA file integration for streaming files such as XA audio and MDEC STR video... Thus, MKPSXISO was made to replace BUILDCD to aid PlayStation homebrew development on modern systems.

MKPSXISO more or less replicates BUILDCD's functionality but better! The most notable difference is that MKPSXISO produces ISO images in either iso or cue+bin format so that generated images can immediately be loaded to an emulator or burned to a CD. The cue+bin format allows images to contain more than a single track usually for iso image projects that include CD-DA audio tracks.

Another notable difference is that MKPSXISO injects the Sony license data correctly into the disc image. However, the license data is not provided with the program so one must have a copy of the official PlayStation Programmer's Tool SDK (which can be found in www.psxdev.net) for the license data. This is to avoid possible legal problems when including Sony's license data into open source programs and it's better to be safe than sorry... Besides, there already exists disc patcher tools to inject license data if you don't use the PlayStation Programmer's Tool SDK anyway.

## Features
* Outputs CD image in iso or bin+cue image format.
* Injects license data properly.
* Manual file ordering to allow for file seek optimization.
* Supports mixed-mode XA file integration for CD-XA streams such as XA audio and STR video.
* Supports injecting CD-DA audio tracks into generated ISO image.
* Uses XML for iso project scripting.

## Binary Download
The latest precompiled Win32 binary of this program can be downloaded here:
[mkpsxiso-1.14.zip](http://lameguy64.github.io/mkpsxiso/mkpsxiso-1.14.zip)

Older versions:
[mkpsxiso-1.10.zip](http://lameguy64.github.io/mkpsxiso/mkpsxiso-1.10.zip)
[mkpsxiso-1.06.zip](http://lameguy64.github.io/mkpsxiso/mkpsxiso-1.06.zip)
[mkpsxiso-1.04.zip](http://lameguy64.github.io/mkpsxiso/mkpsxiso-1.04.zip)
[mkpsxiso-1.00.zip](http://lameguy64.github.io/mkpsxiso/mkpsxiso-1.00.zip)

## Compiling
This tool requires tinyxml2 to compile.

### Windows (CodeBlocks without CMake)
1. Install CodeBlocks (Preferably with MinGW32 GCC compiler bundled).
2. Extract and compile tinyxml2 in the root of your C: drive (C:\tinyxml2).
3. Make sure the tinyxml2 library is named libtinyxml2.a.
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
**Version 1.14 (BIG update because I forgot to release 1.12)**
* Name attribute of file entries can now be omitted provided that the source attribute is at least present.
* Changed some char* strings to std::string strings.
* Fixed typo in help text where -lba was referenced as -lbalist.
* Fixed system and application identifiers not defaulting to PLAYSTATION when blank.
* Fixed bug where mkpsxiso sometimes reports an error of specifying -o on a multi-disc ISO project when the project only has one ISO project.
* Fixed a major LBA calculation bug where files written past a directory record spanning more than one sector are not assigned to an LBA address where it can safely be written. This resulted to the file getting overwritten by the directory record and thus, leads to file corruption.
* Fixed file overwrite confirmation bug still being prompted when -noisogen is specified.
* Fixed a bug where the length of directory records is always written with a length of 2048 bytes which may have caused some ISO browsing programs to throw a fit when browsing the generated ISO images.
* Changed the format of the file LBA log completely which no longer uses a tree style format but it outputs more information of the file entries.
* LBA values in generated header files are now offset by 150 sectors to take into account that sectors actually begin past 150 sectors. This may have caused direct LBA seeks to point 150 sectors behind the actual LBA on the CD.

**Version 1.10**
* Can now handle as many files/directories the ISO9660 filesystem can handle without crashing or reporting an error as both a new feature and a fix for MeganGrass.
* 2 second pregaps are now generated for CD audio tracks 3 and onwards in ISO projects that contain CD Audio.
* Added -noisogen to disable iso generation useful for generating only an LBA listing of files in the ISO project in plain text or C header format.
* Added -lbahead which is similar to -lba but outputs in C header format.
* Fixed a bug where mkpsxiso would produce a 0 byte image file when an error occurs while using the -o parameter to specify the output image file.
* Added optional srcdir attribute for -directory_tree- and -dir- elements (mostly intended for the new mkisoxml tool).
* Added crude mkisoxml utility for quickly generating basic ISO XML projects out of a directory (can be compiled with CMake).

**Version 1.06**
* Added file overwrite confirmation.
* Element and attribute strings are no longer case sensitive.
* File name strings from arguments are no longer passed as duplicated strings (eliminates the risk of forgetting to dealloc said strings on exit).
* Added -lba option to produce a complete listing of files and directories in the ISO file system with their LBA addresses.
* Added -dummy- element example in example.xml (forgot to put it in since the first release).

**Version 1.05 (by electroCupcake)**
* Fixed types for linux build, changed u_char and such to unsigned char.
* In cygwin64 version of tinyxml2, "XML_NO_ERROR" is not defined, changed with "XML_SUCCESS" and this works on both Windows and Linux.
* Converted to cmake, if you want a codeblocks project file, just run "cmake . -G "CodeBlocks - Unix Makefiles"" to create it.

**Version 1.04**
* Fixed a bug where you'll get a 'Data track must only be on first track' error when creating an ISO image with more than 2 tracks even when the extra tracks are CD audio.
* Duplicate file and directory entries are no longer possible to add (it'll result to weird problems anyway).

**Version 1.00**
* Initial release.
