This is a fork of isodump made by [Lameguy64](https://github.com/Lameguy64/).
As it seems isodump is abandoned, the goal of this fork is to fix all the major bugs the original isodump had, and
eventually add new features.

Currently, the tool can be built on linux using a Makefile and on Windows via Visual C++ or CodeBlocks.

This tool is meant to extract the contents of a PSX game iso/bin file and at the same time, generate a layout of the image to an xml file. This xml will be useful to rebuild the iso using mkpsxiso by [Lameguy64](https://github.com/Lameguy64/).

Windows binaries are released in the releases section of this repo.

## Known Issues
- The current version does not support cue sheets but only iso/bin files. This means that the audio tracks of the iso will be lost during extraction in the following cases:
  - the game's audio tracks are stored together with the data track in the iso file, and the audio tracks are not referenced by a corresponding CDDA file in the data track. In this case, isodump will "miss" the audio data. Thus, it is better to have the data track separated from the audio tracks, and then manually modify the generated xml to let mkpsxiso include the audio tracks in the rebuilt iso.
  - the game's audio tracks are stored in separate files than the main iso, and the audio tracks are referenced by some CDDA file in the data track. In this case, isodump will not be able to read the audio data referenced by the CDDA files. Thus, it is better to merge all tracks into one bin file, e.g., with [binmerge](https://github.com/putnam/binmerge).

- This is really not related to isodump, rather to mkpsxiso. mkpsxiso rebuilds bins not necessarily following the internal layout of some game images. See for example Vagrant Story of Final Fantasy 7. So, even if no files are changed, the rebuilt bin will assign different LBAs to the files. This might be an issue for games that use their own internal LBA table (e.g., FF7), rather than the iso9660 TOC.

Below you can find the original README by Lameguy64.

# ISODUMP
A very basic PlayStation ISO dumping tool that extracts the contents of a PlayStation ISO file. It currently only supports extracting files from the ISO9660 file system which includes standard data files, XA and STR files. It also supports generating an MKPSXISO compatible XML project file so the ISO may be rebuilt from the extracted files.

This tool is still in alpha phase and was quickly put together on a whim. Therefore, a lot has yet to be improved with this program such as improving XA/STR extraction logic as some XA/STR files are either not recognized properly or is not extracted properly, most notably with Crash Team Racing.

## Binary Download
The latest precompiled Win32 binary of this program can be downloaded here:
[isodump-0.25a.zip](http://lameguy64.github.io/isodump/isodump-0.25a.zip)

## Compiling
This tool requires tinyxml2 to compile.

### Windows (CodeBlocks without CMake)
1. Install CodeBlocks (Preferably with MinGW32 GCC compiler bundled).
2. Extract and compile tinyxml2 in the root of your C: drive (C:\tinyxml2).
3. Make sure the tinyxml2 library is named libtinyxml2.a.
3. Open the project file isodump.cbp inside the project directory.
4. Press Ctrl+F9 to compile the program.
5. The result will be in the base folder named "mkpsxiso.exe"

## Changelog
**Version 0.25a**
* Initial release.
