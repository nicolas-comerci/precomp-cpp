Precomp Neo
===========

Why does this fork exist?
-------------------------
It started when on mid 2022 (around 9 months ago, at the time I am writing this) I attempted to add stdin/stdout support to Precomp, so we could use it without needing to write to or read from massive files.
OTF compression supported by Precomp was not an ideal solution as it prevented us from using other tools (like specialized data deduplicators like SREP for example).

In any case I got fairly advanced with the project (there is even an MR on Precomp's github https://github.com/schnaader/precomp-cpp/pull/140) but pretty quickly ran into problems.
The code was pretty hard to get into because it wasn't organized in any modular fashion, with most of the code on a single file.
The more I looked at the code the more things I saw that I realized I could improve.

So I decided to just do it! The reason I tackled it as a fork instead of contributing to the main project is that it would have been too much work, it would have meant MASSIVE MRs,
which would have looked to be doing unnecessary things unless I spent inordinate amounts of time explaining how those things made sense because of what I was planning next, and I
figured I would probably not even do it if I had to do that. Sorry if it makes things inconvinient in terms of contributing pieces from my fork to the main Precomp project, but again,
I don't think it would have been too feasible to do this work otherwise.

Okay, but in the end what are the changes in this fork?
-------------------------------------------------------
- **LibPrecomp C API which allows other programs/libraries to use precomp's capabilities**
    - New libprecomp.h header defines the C API
    - Precomp is now just a program that uses LibPrecomp, it has no privileged access to LibPrecomp's internals at all.
        - Moved most of the code actually relevant to Precomp from precomp.cpp to precomp_dll.cpp.
        - precomp.cpp still actually uses precomp_utils and precomp_io directly so that might be considered privileged access, should be easy to remove.
    - dlltest.cpp is now dlltest.c, a pure C program, still WAY less capable than precomp.cpp, but showcases how LibPrecomp can be used from non C++, actual C.
    - All of the prints to the console/terminal got changed for a rudimentary logging system which allows consumers of the library to get the feedback and decide what to do with it.
        - So we don't spam the library consumer program's terminal with unwanted printing.
    - Similarily, reacting to progress is now done with a callback, so the consuming program can decide what to do with that.
- **"Modularized" the code**
    - Supporting precomp_io and precomp_utils files which have code that is not directly related to Precomp's execution flow or structure or intrinsically related to supporting any specific file format.
    - Code related to supporting precompression/recompression of a specific file format now lives in corresponding files on the 'formats' folder
- **"Modernized" the code**
    - A lot of the code has been modified to rely more heavily on C++ features (like we have temporary files that clean after themselves using RAII), and the C++ specification version has been bumped to C++20
- **Extended capabilities**
    - Stdout support for precompression, Stdin/Stdout support for recompression (makes sense, given this is the whole point of what I wanted to do initially)
        - dlltest.c also has this support
    - C API allows to set up what I call 'Generic Input/Output Streams' which are essentially just istreams/ostreams you can make from C by using function pointers.
        - This essentially means LibPrecomp can take streaming input or stream output to and from pretty much anywhere, you could be encrypting or decrypting, compressing or decompressing, whatever its up to you.
- **Streamlined (french for stuff got deleted)**
    - OTF compression is gone. In my opinion the new extended capabilities render OTF compression redundant. You can just pipe to/from bzip2/7za/xz/gzip.
        - This allowed me to delete a non trivial amount of code handling special cases for OTF compression, which makes the code easier to work with
    - With OTF compression, the whole 'convert' feature got cut as well, as it makes no sense if Precomp doesn't have different compression formats to convert between.
    - This 2 deletions simplified Precomp's command line as all the OTF, conversion and LZMA parameters, with their parsing and setting code got deleted.
    - Precompf got the axe as well
        - Seemed mostly useless for me without OTF compression, and even if we still had it, I didn't see much utility on having most of the parameter parsing code duplicated but reading from an INI file.
        - Tons of removed code, and removed a decent amount of preprocessor conditionals, so I would argue this also makes the code easier to understand, specially for newcomers.
        - But it lives on! Sort of. There is now a -comfort switch that looks for a PCF header on the input stream, and performs recompression if found, precompression if not found.
            - This paired with a simple CMD file (precompf.cmd, included) where you set your preferred settings is able to replicate the old precompf behavior.
                - Can also be done with a sh file most likely, but didn't look into it for now.
    - Brotli support for JPG got removed.
        - Had a hell of a binary footprint on our library/executables.
        - Brunsli code got restored so it doesn't have any Precomp specific modifications, which MIGHT make it easier to update it in the future, but who knows.
        - Now we have a fake Brotli library that has everything Brunsli needs, but just uses memcpy to read or write as the modified Brunsli code did.
- **Other improvements**
    - On brute mode the previous histogram's data (the one used to reject false positives) is quickly adapted and reused if we are checking for the next position on the input stream.
        - This yields varied results on my testing, from a ~10% PESSIMISATION on very small streams to a massive optimisation of 70% on large streams with many false positives.
            - The pessimisation on the small streams is not really important because they are small so they are processed quickly anyways, and the typical results are much better.
    - During recompression recursion we don't need to copy the whole recursive input data to a temporary file, it is read from the input stream directly.
    - During recompression recursion we don't need to output to a temporary file either, the output is generated from a separate thread that does the recursive recompression and the calling thread reads it as its available.
    - These 2 changes mean that the free space requirements + IO load of recompression is heavily decreased.
    - Bzip2 recompression penalty bytes are patched as the recompressed data is being written, instead of having to seek on the output stream to patch the data (which is not possible on piped output).
    - Temporary files are prepended with a random string, so multiple instances of precomp can work on the same directory without overwriting/reading other instances files. (fixes https://github.com/schnaader/precomp-cpp/issues/102, very likely renders https://github.com/schnaader/precomp-cpp/issues/74 irrelevant/unnecessary) 

Despite the large amount of changes, Precomp Neo should be mostly compatible and be able to recompress Precomp v0.4.8 PCF files.
The previously mentioned exceptions apply, OTF compressed files or files using Brotli won't work.
If your PCF file has Brotli compressed JPGs you are out of luck and will need to recompress using mainline Precomp and precompress again using Precomp Neo.
For OTF compressed PCF files you can use mainline Precomp's convert feature to get an uncompressed PCF file which Precomp Neo should be able to recompress.

Great, but what happens now?
----------------------------
While I have tested this with a lot of files, I need to continue testing, specially against mainline Precomp to ensure I fix any new bug I have introduced during this whole refactoring project.
Of course if I can fix any already existing bug in precomp I run into while testing I will probably do so.

In so far as this fork and mainline precomp, or how to get improvements from this fork into precomp, I have no idea.
I would probably have to consult with Schneider if he is interested, in what of the improvements he is most interested, how we could tackle it, etc.

For now, I will continue working on this on my spare time, my main focus being fixing bugs, and improving reliability.
Adding extra formats and making Precomp even more powerful is something I would like to do, but for now I would love to get Precomp into a more 'production ready' state with the format support it already has.
Like I would like it if I didn't need to immediately recompress and hash check the output of precomp against the original file to make sure I am actually recovering the original file.
Get it to a level where other products can confidently use libprecomp without worrying their data might get corrupted.

If you want to contribute to this fork, feel free to do so! I do think it should be much easier to get into it than mainline Precomp so take a look around the code.

Contact
-------
You can reach me at nicolas.comerci@fing.edu.uy.

However, please do not contact me by email if another channel would be more appropriate.
In particular, don't ask for features, improvements, bug fixes or format support requests.
The github issues page on the repo is the appropriate channel for those subjects, you WILL be flagged as spam and ignored if you email me about these things.

**ORIGINAL PRECOMP README BELOW**

Precomp
=======

[![Join the chat at https://gitter.im/schnaader/precomp-cpp](https://badges.gitter.im/schnaader/precomp-cpp.svg)](https://gitter.im/schnaader/precomp-cpp?utm_source=badge&utm_medium=badge&utm_campaign=pr-badge&utm_content=badge)
[![Build Status](https://travis-ci.com/schnaader/precomp-cpp.svg?branch=master)](https://travis-ci.com/schnaader/precomp-cpp)
[![Build status](https://ci.appveyor.com/api/projects/status/noofdvr23uk2oyyi/branch/master?svg=true)](https://ci.appveyor.com/project/schnaader/precomp-cpp)

[![Packaging status](https://repology.org/badge/vertical-allrepos/precomp.svg)](https://repology.org/metapackage/precomp)

What is Precomp?
----------------
Precomp is a command line precompressor that can be used to further compress files that are already compressed. It improves compression on some file-/streamtypes - works on files and streams that are compressed with zLib or the Deflate compression method (like PDF, PNG, ZIP and many more), bZip2, GIF, JPG and MP3. Precomp tries to decompress the streams, and if they can be decompressed and "re-"compressed so that they are bit-to-bit-identical with the original stream, the decompressed stream can be used instead of the compressed one.

The result of Precomp is either a smaller, LZMA2 compressed file with extension .pcf (PCF = PreCompressedFile) or, when using `-cn`, a file containing decompressed data from the original file together with reconstruction data. In this case, the file is larger than the original file, but can be compressed with any compression algorithm stronger than Deflate to get better compression.

Since version 0.4.3, Precomp is available for Linux/*nix/macOS, too. The different versions are completely compatible, PCF files are exchangeable between Windows/Linux/*nix/macOS systems.

Usage example
-------------
|Command|Comment|
|--|--|
|`wget http://mattmahoney.net/dc/silesia.zip` <br> (or download from [here](http://mattmahoney.net/dc/silesia.html))|We want to compress this file (the [Silesia compression corpus](http://sun.aei.polsl.pl/~sdeor/index.php?page=silesia)). <br>Size: 67,633,896 bytes (100,0%)|
|`7z a -mx=9 silesia.7z silesia.zip`|Compressing with [7-Zip](https://www.7-zip.org/) LZMA2, setting "Ultra". <br>Size: 67,405,052 bytes (99,7%)|
|`precomp silesia.zip`|Compressing with Precomp results in `silesia.pcf`. <br>Size: 47,122,779 bytes (69,7%)|
|`precomp -r -osilesia.zip_ silesia.pcf`|This restores the original file to a new file named `silesia.zip_`. <br> Without the `-o` parameter, Precomp would decompress to `silesia.zip`.|
|`diff -s silesia.zip silesia.zip_`|Compares the original file to the result file, they're identical|


How can I contribute?
---------------------
* You can have a look at the [Issue list](https://github.com/schnaader/precomp-cpp/issues)
  * If you are looking for easy issues that don't require deeper understanding of the whole project, look for [issues with the `low hanging fruits` tag](https://github.com/schnaader/precomp-cpp/labels/low%20hanging%20fruits)
* You can build the project or download the binaries (see below), run it on your system and report bugs or make enhancement proposals.

Releases/Binaries
-----------------
[Official GitHub releases](https://github.com/schnaader/precomp-cpp/releases) for both Windows and Linux.

[Alternative binary download](http://schnaader.info/precomp.php#d) of the latest official release for both Windows and Linux.

Binaries for older version can be found at [this Google Drive folder](https://drive.google.com/open?id=0B-yOP4irObphSGtMMjJSV2tueEE).

Contact
-------
Christian Schneider

schnaader@gmx.de

http://schnaader.info

Donations
---------
[![Donate](https://img.shields.io/badge/Donate-PayPal-green.svg)](https://www.paypal.com/cgi-bin/webscr?cmd=_s-xclick&hosted_button_id=X5SVF9YUQC9UG)

To donate, you can either use the donate button here, the one at the top of the page ("Sponsor") or the one on [my homepage](http://schnaader.info). You can also send bitcoins to

    1KvQxn6KHp4tv92Z5Fy8dTPLz4XdosQpbz

Credits
-------
Thanks for support, help and comments:

- Stephan Busch (Squeeze Chart Author, http://www.squeezechart.com)
- Werner Bergmans (Maximum Compression Benchmark Author, http://www.maximumcompression.com)
- Matthias Stirner (packJPG, packMP3, https://github.com/packjpg, http://www.matthiasstirner.com)
- Mark Adler (http://www.zlib.net)
- Matt Mahoney (http://www.mattmahoney.net)
- Malcolm Taylor (http://www.msoftware.co.nz)
- Simon Berger (helped to fix many bugs)
- The whole encode.su forum (https://encode.su/)

Legal stuff
-----------
- brunsli (https://github.com/google/brunsli) by Google LLC is used for compression/decompression of JPG files.
- brotli (https://github.com/google/brotli) by the Brotli Authors is used for compression/decompression of JPG metadata.
- packJPG v2.5k (https://github.com/packjpg/packJPG) by Matthias Stirner is used for compression/decompression of JPG files.
- packMP3 v1.0g by Matthias Stirner is used for compression/decompression of MP3 files.
- bZip2 1.0.6 (http://www.bzip.org) by Julian Seward is used for compression/decompression of bZip2 streams.
- zLib 1.2.11 (http://www.zlib.net) by Jean-loup Gailly and Mark Adler is used for compression/decompression of zLib streams.
- GifLib 4.1.4 (http://sourceforge.net/projects/giflib) is used for compression/decompression of GIF files. The GIFLIB distribution is Copyright (c) 1997 Eric S. Raymond
- liblzma from XZ Utils 5.2.3 (http://tukaani.org/xz) is used for compression/decompresson of lzma streams.
- preflate v0.3.5 (https://github.com/deus-libri/preflate) by Dirk Steinke is used to create and use reconstruction information of deflate streams

License
-------
Copyright 2006-2021 Christian Schneider

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
