# MKPSXISO
MKPSXISO is basically a modern clone of BUILDCD used for building CD images of PlayStation games in the official development tools. The problem with BUILDCD however is, apart from licensing issues, its an old 16-bit DOS program which will not work natively under 64-bit versions of Windows without a DOS emulator which not only makes automated build scripts that produce ISO images messy in homebrew development but it also slows ISO creation speed considerably. BUILDCD also only produces CD images in a special image format supported by early CD burners and must be converted to an ISO format with a separate tool making the already slow ISO creation process even slower.

While other ISO creation tools such as MKISOFS may work as an alternative most do not let you control the order of the files stored in the ISO image which is essential for optimizing file order to speed up access times and all do not support mixed-mode type files for CD streaming such as XA audio and MDEC video streams. MKPSXISO is made specifically to replace BUILDCD to aid in PlayStation homebrew development on modern systems as well as modification/hacking of existing PlayStation titles. MKPSXISO can also be used as a regular ISO creation tool that complies with the older ISO9660 standard with no Joliet extensions.

MKPSXISO more or less replicates most of the functionality of BUILDCD but better! The most notable difference is that MKPSXISO is much faster and creates ISO images in either standalone iso or cue+bin format so that generated images can immediately be run on an emulator or burned to a CD.

Another notable difference of MKPSXISO is that it injects the Sony license data correctly into the disc image which eliminates the need of having to use a separate program for properly licensing the ISO image. However, the license data is not included so one must have a copy of the official PlayStation Programmer's Tool SDK or the PsyQ SDK (both of which can be found in www.psxdev.net) for the license files to be able to take advantage of this feature. This is to avoid possible legal problems when including Sony's license data into open source programs.

## Features
* Uses XML for scripting ISO projects.
* Outputs ISO images directly to iso or bin+cue image format.
* Injects license data into ISO image correctly.
* File LBA controlled by order of files allowing for file seek optimization (just like BUILDCD).
* Supports mixed-mode CD-XA stream files such as XA audio and STR video.
* Supports CD-DA audio tracks from uncompressed WAV files either as plain tracks or DA/WAV files.
* Can output log of all files packed with details such as LBA, size and timecode offset.

## Binary Download
The latest Win32 binaries is now a release download in this repository.

Older versions (probably going to be removed soon, there's no benefit to using these versions anyway):
* [mkpsxiso-1.20.zip](http://lameguy64.github.io/mkpsxiso/mkpsxiso-1.20.zip)
* [mkpsxiso-1.19.zip](http://lameguy64.github.io/mkpsxiso/mkpsxiso-1.19.zip)
* [mkpsxiso-1.18.zip](http://lameguy64.github.io/mkpsxiso/mkpsxiso-1.18.zip)
* [mkpsxiso-1.15.zip](http://lameguy64.github.io/mkpsxiso/mkpsxiso-1.15.zip)
* [mkpsxiso-1.14.zip](http://lameguy64.github.io/mkpsxiso/mkpsxiso-1.14.zip)
* [mkpsxiso-1.10.zip](http://lameguy64.github.io/mkpsxiso/mkpsxiso-1.10.zip)
* [mkpsxiso-1.06.zip](http://lameguy64.github.io/mkpsxiso/mkpsxiso-1.06.zip)
* [mkpsxiso-1.04.zip](http://lameguy64.github.io/mkpsxiso/mkpsxiso-1.04.zip)
* [mkpsxiso-1.00.zip](http://lameguy64.github.io/mkpsxiso/mkpsxiso-1.00.zip)

## Compiling

1. Set up CMake and a compiler toolchain. Install the `cmake` and `build-essential` packages provided by your Linux distro, or one of the following kits on Windows:
   * MSVC (do not install CMake through the Visual Studio installer, download it from [here](https://cmake.org/download) instead)
   * MSys64 (use the "MinGW 64-bit" shell) with the following packages: `git`, `mingw-w64-x86_64-make`, `mingw-w64-x86_64-cmake`, `mingw-w64-x86_64-gcc`
   * Cygwin64 with the following packages: `git`, `make`, `cmake`, `gcc`

2. Clone/download the repo, then run the following command from the mkpsxiso directory to ensure `tinyxml2` is also downloaded:

   ```bash
   git submodule update --init
   ```

3. Run the following commands:

   ```bash
   cmake -S . -B ./build -DCMAKE_BUILD_TYPE=Release
   cmake --build ./build
   cmake --install ./build
   ```

   Add `sudo` to the install command if necessary.

The default installation path is `C:\Program Files\mkpsxiso\bin` on Windows or `/usr/local/bin` on Linux. You can change it to any directory by passing `--install-prefix` to the first command.

## Issues

The only known major issue that hasn't (or cannot) be resolved is that if you create a disc image with the following directory structure:
```xml
<dir name="dira">
    <dir name="subdir1a">
        <dir name="subdiraa"/>
        <dir name="subdirab"/>
        <dir name="subdirac"/>
    </dir>
    <dir name="subdir1b"/>
    <dir name="subdir1c"/>
</dir>

<dir name="dirb">
    <dir name="subdir2a"/>
    <dir name="subdir2b"/>
    <dir name="subdir2c">
        <dir name="subdirba"/>
        <dir name="subdirbb"/>
        <dir name="subdirbc"/>
    </dir>
</dir>

<dir name="dirc">
    <dir name="subdir3a"/>
    <dir name="subdir3b"/>
    <dir name="subdir3c"/>
</dir>
```

On Windows, browsing the subdirectories in dirb and dirc will not list the contents for some reason and trying to access it in a command prompt leads to a permission denied or similar error message. Disc image tools such as CDmage will display the contents of the aforementioned subdirectories without issue and the issue persists on disc images created with BuildCD suggesting it is likely an operating system related issue and not an ISO generator issue.

This can be avoided by minimizing identically named directories but its best to test your generated disc image before considering it ready for release.

## Changelog

**Version 1.27**
* Fixed stringop overflow bug when temporarily clearing sector address bytes.
* Path is now stripped for the .bin file specification of cue sheets.

**Version 1.25 (12/30/2020)**
* Replaced xa and str modes with "mixed" mode (see example.xml for details). xa and str modes are now just aliases to the new "mixed" mode, for backwards compatibility.

**Version 1.24 (12/28/2020)**
* Fixed EDC generation for STR files.

**Version 1.23 (12/20/2018)**
* Fixed broken LBA and timecode calculation for audio tracks integrated as files (iso::DirTreeClass::GetWavSize returns an incorrect value from the WAV file).
* Updated build instructions (CodeBlocks project had been replaced with Netbeans but forgot to update instructions).

**Version 1.22 (12/4/2018)**
* Fixed issues with subheader detection logic and made it as a warning instead of a critical error.
* Fixed bug where CD-DA length of possibly being 1 sector larger than it should.

**Version 1.21 (7/8/2018)**
* Corrected volume size calculation logic when DA audio files are included. Also fixed volume size value being 2 sectors larger than it should.
* Corrected LBA header file output where the 150 sector pregap offset is not needed (libcd already does that).
* Fixed path name being double appended when using srcdir attribute (this made mkpsxiso unable to parse an xml document produced by mkisoxml).
* Added -noxa option and no_xa XML attribute to disable CD-XA attribute creation (plain ISO9660).
* Fixed quiet mode preventing cue_sheet attribute from being parsed.
* Added RIFF header and XA/STR subheader checks to error on improperly ripped or encoded XA/STR source files.
* Improved data sector detection for STR type files (Mode2/Form1 + Mode2/Form2 interleaves).
* Implemented proper EOL/EOF bits for subheaders at the ends of descriptors and files.
* Fixed an XML parser bug where name attribute is used as the source file name when a source directory in a directory element is specified.
* Fixed a major bug where directory record lengths are always set at 2048 bytes even for directory records spanning more than a sector resulting in missing files and directories.
* Fixed incorrectly calculated record length set in the volume descriptor's root directory record.
* Added copyright XML attribute for the identifiers element to specify a copyright identifier for the image file.

**Version 1.20 (6/21/2018)**
* ISO names being blank or garbage when compiled using Microsoft's compiler has been fixed.
* Replaced tinyxml2::GetErrorLineNum() method calls to tinyxml2::ErrorLineNum() for compatibility with the latest version of tinyxml2.
* Fixed incorrect file size calculated for STR and DA audio files.
* DA audio type files are now set with a CD-Audio attribute making them appear as WAV files when opening them (note: it does not turn the DA audio into a WAV file, the OS supporting said attribute transparently converts it into a WAV container as you read it, one game featuring such files is Wipeout 3 for example).
* Brought back mkisoxml (just needed to be recompiled with the latest GCC).

**Version 1.19 (6/12/2018)**
* Path table generation logic significantly reworked. Previous implementation was flawed and caused issues on games and operating systems that utilize the path table. MKPSXISO should be truly ISO9660 compliant now (apologies for the misleading remark in the 1.15 changelog).
* Date stamp is now set in the root directory record in the image descriptor.
* Fixed specifying only a source attribute for a file element not behaving as intended.
* Fixed no error on missing CUE file when DA audio type files are found but no cue sheet was specified.

**Version 1.18 (5/16/2018)**
* Added support for DA audio files (files that map directly to a CD Audio track which games that use CD Audio have).
* XML load error message now reports useful error details.
* Parameter errors in XML script now reports with line numbers.
* Fixed corrupt file names displayed when packing files.
* Fixed corrupt directory names and corrected minor formatting issues of generated LBA header files.
* Improved formatting of generated LBA listings.
* Can now accept raw audio files for CD audio tracks. However, it assumes any file that isn't a WAV file as raw audio.
* Directory record entries now handled using std::vector arrays (possibly better stability).
* Replaced char strings to std::string strings inside DirTreeClass and associated typedefs.
* Replaced all strcasecmp() with custom compare() function to make code fully C++14 compatible.
* Included Netbeans 8.1 project.
* Removed mkisoxml from download temporarily (currently too buggy, source still available).

**Version 1.15 (6/16/2017)**
* Directory record lengths have been tweaked to now calculate in sector multiples instead of actual bytes. This now makes ISOs generated with MKPSXISO fully ISO9660 compliant and ISO tools that threw a fit when opening ISO images generated with older versions of MKPSXISO should no longer complain.
* Improved XA attribute header encoding (not really necessary but still nice to have).
* Re-done README text.

**Version 1.14 (6/4/2017, BIG update because I forgot to release 1.12)**
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

**Version 1.10 (2/23/2017)**
* Can now handle as many files/directories the ISO9660 filesystem can handle without crashing or reporting an error as both a new feature and a fix for MeganGrass.
* 2 second pregaps are now generated for CD audio tracks 3 and onwards in ISO projects that contain CD Audio.
* Added -noisogen to disable iso generation useful for generating only an LBA listing of files in the ISO project in plain text or C header format.
* Added -lbahead which is similar to -lba but outputs in C header format.
* Fixed a bug where mkpsxiso would produce a 0 byte image file when an error occurs while using the -o parameter to specify the output image file.
* Added optional srcdir attribute for -directory_tree- and -dir- elements (mostly intended for the new mkisoxml tool).
* Added crude mkisoxml utility for quickly generating basic ISO XML projects out of a directory (can be compiled with CMake).

**Version 1.06 (11/28/2016)**
* Added file overwrite confirmation.
* Element and attribute strings are no longer case sensitive.
* File name strings from arguments are no longer passed as duplicated strings (eliminates the risk of forgetting to dealloc said strings on exit).
* Added -lba option to produce a complete listing of files and directories in the ISO file system with their LBA addresses.
* Added -dummy- element example in example.xml (forgot to put it in since the first release).

**Version 1.05 (by electroCupcake)**
* Fixed types for linux build, changed u_char and such to unsigned char.
* In cygwin64 version of tinyxml2, "XML_NO_ERROR" is not defined, changed with "XML_SUCCESS" and this works on both Windows and Linux.
* Converted to cmake, if you want a codeblocks project file, just run "cmake . -G "CodeBlocks - Unix Makefiles"" to create it.

**Version 1.04 (9/1/2016)**
* Fixed a bug where you'll get a 'Data track must only be on first track' error when creating an ISO image with more than 2 tracks even when the extra tracks are CD audio.
* Duplicate file and directory entries are no longer possible to add (it'll result to weird problems anyway).

**Version 1.00 (8/6/2016)**
* Initial release.
