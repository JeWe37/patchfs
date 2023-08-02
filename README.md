# PatchFS

PatchFS is a FUSE filesystem that allows you to mount a directory and apply patches to the files contained in it.

This is particularly useful for saving space when having multiple similar large files.

The files are patched on the fly, so the original files, specified via xattr, are not modified.

Use of mmap ensures that only the parts of the files that are actually used are read from disk. While a file is open, the diff is stored in memory. Therefore it is recommended to only use small diffs.

The diffs themselves are stored in VCDIFF format, an encoder for which is included in the source. This encoder is based on the one from [open-vcdiff](https://github.com/google/open-vcdiff).

The patching itself is done using [tiny-vcdiff](https://github.com/jue89/tiny-vcdiff).

## Compiling

Compiling required CMake, libfuse and a C/C++ compiler. Then run the following commands:
```bash
cmake -B build -S .
cmake --build build
```

## Usage

In order to generate the diffs, you can use the included encoder:
```bash
./build/bin/encoder [OLD] [DIFF] [OLD_PATH] [NEW]
```
where the `OLD_PATH` is the path in the base directory.

Then you can mount the filesystem:
```bash
./build/bin/vcdiff-fuse -o base=[BASE] [DIFFDIR] [MOUNTPOINT]
```
