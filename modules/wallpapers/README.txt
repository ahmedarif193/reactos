Wallpapers
===========

All images are resampled to 1920 px wide (Lanczos) and re-encoded as progressive
JPEG at quality 82 with metadata stripped, so the whole set costs ~3.4 MB on the
installation media. "Sea Bridge" ships additionally as a PNG because the
experimental early splash (base/system/winlogon/splash.c) decodes the default
wallpaper with libpng before the shell is up. See LICENSE.txt for the governing
terms, current clearance status, and required provenance record.

|-------------------------|--------------|-------------|-------------------------|
| Filename                | License      | Author      | Origin                  |
|-------------------------|--------------|-------------|-------------------------|
| Calm Lake.jpg           | Restricted*  | Unrecorded  | magnific.com (Freepik)  |
| Feather Droplets.jpg    | Restricted*  | Unrecorded  | magnific.com (Freepik)  |
| Forest Canopy.jpg       | Restricted*  | Unrecorded  | magnific.com (Freepik)  |
| Lake Mountains.jpg      | Restricted*  | Unrecorded  | magnific.com (Freepik)  |
| Lugano Reflections.jpg  | Restricted*  | Unrecorded  | magnific.com (Freepik)  |
| Modern Villa.jpg        | Restricted*  | Unrecorded  | magnific.com (Freepik)  |
| Mountain Lake.jpg       | Restricted*  | Unrecorded  | magnific.com (Freepik)  |
| Night Sky.jpg           | Restricted*  | Unrecorded  | magnific.com (Freepik)  |
| Oil and Water.jpg       | Restricted*  | Unrecorded  | magnific.com (Freepik)  |
| Plant Textures.jpg      | Restricted*  | Unrecorded  | magnific.com (Freepik)  |
| Rain Lights.jpg         | Restricted*  | Unrecorded  | magnific.com (Freepik)  |
| Sea Bridge.jpg          | Restricted*  | Unrecorded  | magnific.com (Freepik)  |
| Sea Bridge.png          | Restricted*  | Unrecorded  | magnific.com (Freepik)  |
|-------------------------|--------------|-------------|-------------------------|

* The exact content type and account tier are not recorded. These images must
not be pushed to upstream ReactOS or shipped in a public release until the
per-file redistribution rights required by LICENSE.txt are documented.

"Sea Bridge" is the default wallpaper. Changing it means updating all four of:

  sdk/include/reactos/early_splash.h    REACTOS_EARLY_SPLASH_FILENAME
  boot/bootdata/hiveearlysplash.inf     HKCU Control Panel\Desktop\Wallpaper
  base/system/winlogon/CMakeLists.txt   add_cd_file under ENABLE_EXPERIMENTAL_EARLY_SPLASH
  modules/wallpapers/CMakeLists.txt     list(REMOVE_ITEM ...) that avoids the duplicate

To include the module in your build folder, run the configure script with the flags -DENABLE_WALLPAPERS=1

# For Windows users

    configure.cmd -DENABLE_WALLPAPERS=1

# For UNIX users

    ./configure.sh -DENABLE_WALLPAPERS=1
