# MKPSXISO
Basically a modern clone of BUILDCD that came with the PsyQ SDK for generating CD images used for developing PlayStation software (usually homebrew nowadays). The problem with BUILDCD however is that it's a very old real-mode DOS program and will not work on 64-bit versions of Windows without a DOS emulator. Also, BUILDCD only produces CD images in an unusual image format used by early CD writers which must be converted to a usable ISO format using a separate tool, making the already slow ISO generation process even slower.

While other ISO generator tools such as the popular MKISOFS may work, most do not let you control the order of the files by LBA (which is very important when optimizing file orders to speed up seek times) and all do not support mixed-mode XA file integration for streamable files such as XA audio and STR MDEC video... Thus, MKPSXISO was made to replace BUILDCD for modern systems.

MKPSXISO more or less replicates BUILDCD's functionality but better, the most notable difference is that MKPSXISO produces images in either iso or bin format so that generated images can immediately be loaded onto an emulator or burned to a CD. The image files may also be paired with a cue sheet for images containing multiple tracks (usually CD audio tracks).

Another notable difference is that MKPSXISO actually injects the Sony license data correctly into the disc image. However, the license data is not provided with the program so one must have a copy of the PsyQ SDK (which can be found in www.psxdev.net) for the license data. This is to avoid possible legal problems when including Sony's license data into open source programs, better to be safe than sorry... Besides, there already exists disc patcher tools that'll inject the license data anyway if you don't use the PsyQ SDK.

## Features
* Outputs in iso/bin format with cue sheet for multi-track projects.
* Correctly injects license data.
* Supports mixed-mode XA file integration for streamable content (XA and STR).
* Can inject CD-DA audio tracks into the image.
* Uses XML for scripting image projects.

## Compiling
This program requires tinyxml2.

To compile for Windows, simply use the CodeBlocks project file and compile it through the IDE. You must use the MinGW GCC compiler (usually included with CodeBlocks) as it has linux specific functions that this program utilizes.

Compiling this program in a Linux environment is not tested yet.

## Binary Download

The precompiled Win32 binaries of this program can be downloaded here:
[mkpsxiso-1.00.zip](http://lameguy64.github.io/mkpsxiso/mkpsxiso-1.00.zip)