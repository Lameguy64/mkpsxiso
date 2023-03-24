
# MKPSXISO

`mkpsxiso` builds PlayStation CD images from an XML document.

`dumpsxiso` dumps PlayStation CD images to files and documents the precise structure to a `mkpsxiso` compatible XML document.

`mkpsxiso` is meant to provide a faster, cross-platform, modern replacement of the BUILDCD from the official development tools. BUILDCD unfortunately only runs on 16 bit DOS compatible systems and it's output format is unusable by modern CD burning tools. Other ISO creation tools such as MKISOFS do not allow controlling the precise order of files (necessary for optimizing access times) and do not support mixed-mode type files for CD streaming such as XA audio and MDEC video streams used by many PlayStation games. `mkpsxiso` outputs either a standard `.bin` and `.cue` or `.iso` ready to burn to CD or use in an emulator! The hope is that `mkpsxiso` tools ease PlayStation homebrew development and ROM hacking and reverse engineer efforts. `mkpsxiso` can also be used as a regular ISO creation tool that complies with the older ISO9660 standard with no Joliet extensions.

`mkpsxiso` can properly license the image with the Sony license data during ISO building eliminating the use of the extra program. However, you must supply your own copy. It can be found in the PsyQ SDK, see [Starting PSX Development](https://psx.arthus.net/starting.html). `dumpsxiso` can also dump the license data of an existing disk.

## Features

* Uses XML for scripting ISO projects.
* Outputs ISO images directly to iso or bin+cue image format.
* Injects license data into ISO image correctly.
* File LBA controlled by order of files allowing for file seek optimization (just like BUILDCD).
* Supports mixed-mode CD-XA stream files such as XA audio and STR video.
* Supports CDDA audio tracks from wav, flac, pcm, and mp3 files, both as DA files and just as audio tracks
* Can output log of all files packed with details such as LBA, size and timecode offset.
* Extract CDDA tracks from ISO as wav, flac, and pcm.
* Many images can be rebuilt 1:1 now.
    * XML generation: by default in strict LBA order, but can instead sort by dir for pretty output.
    * Timestamps and XA attributes are preserved.

## Binary Download

[Releases](../../releases/latest) for Win32 and `ubuntu-latest`, both are built by github CI starting at v2.0

[Ancient releases](https://github.com/Lameguy64/mkpsxiso/tree/gh-pages) (NOT RECOMMENDED)

## Compiling

1. Set up CMake and a compiler toolchain. Install the `cmake` and `build-essential` packages provided by your Linux distro, or one of the following kits on Windows:
   * MSVC (do not install CMake through the Visual Studio installer, download it from [here](https://cmake.org/download) instead)
   * MSys2 (use the "MinGW 64-bit" shell) with the following packages: `git`, `mingw-w64-x86_64-make`, `mingw-w64-x86_64-cmake`, `mingw-w64-x86_64-g++`
   * Cygwin64 with the following packages: `git`, `make`, `cmake`, `gcc`

2. Clone/download the repo, then run the following command from the mkpsxiso directory to ensure `tinyxml2` is also downloaded and updated:

   ```bash
   git submodule update --init --recursive
   ```

3. Run the following commands:

   ```bash
   cmake -S . -B ./build -DCMAKE_BUILD_TYPE=Release
   cmake --build ./build
   cmake --install ./build
   ```

   If you wish to build dumpsxiso without libFLAC support (libFLAC is required for encoding CDDA/DA audio as FLAC), add `-DMKPSXISO_NO_LIBFLAC=1` to the end of the first command.

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

On Windows browsing the subdirectories in 'dirb' and 'dirc' will not list the contents for some reason and trying to access it in a command prompt leads to a permission denied or similar error message. Disc image tools such as CDmage will display the contents of the aforementioned subdirectories without issue and the issue persists with disc images created with BuildCD, suggesting it is likely a bug with the operating system and not mkpsxiso.

This can be avoided by minimizing identically named directories but its best to test your generated disc image before considering it ready for release.


## Changelog

**Version 2.03 (xx/xx/2023)**
* On platforms where `std::filesystem` is unavailable, `ghc::filesystem` is now used instead.
* Switched back to main `libflac`.
* mkpsxiso: Resolved a possible crash when building images with big files.
* mkpsxiso: Fixed an ISO generation bug when building images with directories with many files.
* mkpsxiso: Removed a legacy `-nolimit` command line option.
* dumpsxiso: Output a Copyright field to the XML.

**Version 2.02 (06/24/2022)**
* Fixed NOMINMAX redefinition warnings.
* Added -c|--cuefile argument to specify the cue sheet file name. Overrides the name defined in the XML file.
* Output file is derived from the XML project file name if no output file was specified by arguments or the image_name attribute.
* Added -l|--label argument to specify the volume ID string. Overrides the volume ID defined in the XML file.
* Added id_file attribute for the <identifier> element. Reads identifiers from a separate XML file containing a single <identifier> element.
* Added ISO descriptor check in dumpsxiso.

**Version 2.01 (02/10/2022)**
* Fixed invalid sectors generated when no license file is specified.
* Fixed wide char string specifier for MinGW.
* Added a bunch of fflush() calls to make sure build messages are output at all times.
* Improved help text.

**Version 2.00 (02/02/2022)**
* Added tinyxml2 as a submodule, so manual installation is no longer needed and built binaries will always be statically linked.
* Add `dumpsxiso` the successor to `isodump`. Use `dumpsxiso` to unpack `.bin` isos and `mkpsxiso` to repack.
* Make xml file paths relative to the XML.
* Unify CDDA tracks and DA files to reflect the reality DA files are just links to CDDA tracks by LBA.
* Add `<pregap>` element to `<track>` for specifying the pregap. `dumpsxiso` attempts to be intelligent about guessing the pregap.
* Add `<dummy>` element to specify non-file sectors in an ISO in order to preserve LBA order
* Add packing flac, pcm, and mp3 files as CDDA tracks/DA files in addition to wav. `dumpsxiso` can extract as wav, flac, and pcm.
* Add memory mapped ISO writing to improve packing speed
* Fix directory records spanning more than 1 sector
* dumpsxiso: add group by directory/pretty xml writing
* Fix ECC and timestamp bugs
* Massive refactor and cleanup, too many fixes and changes to list, see the commits for details

**Version 1.27 (10/25/2021)**
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
