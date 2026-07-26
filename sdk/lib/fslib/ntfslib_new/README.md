# ReactOS NTFS Library
Designed to be shared between usermode and kernelmode applications, this library provides everything you need to work with NTFS formated filesystems.

## Targets
To facilitate sharing between different enviornments, you need to link an enviornment specific library in addition to the portable NTFS lib. A list of libraries is provided below.

- `ntfslib_new`: A portable NTFS library
- `ntfslib_new_km`: Extensions to use `ntfslib_new`in Kernelmode.
- `ntfslib_new_um`: Extensions to use `ntfslib_new` in Usermode.
- `ntfslib_new_fl`: Extensions to use `ntfslib_new` in FreeLdr code. (planned, not implemented yet)
