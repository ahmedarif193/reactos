/*
 * Resource/forwarder-only DLLs can end up being linked as an executable
 * (missing IMAGE_FILE_DLL) in some build configurations if no C sources
 * are present. Keep a trivial TU to force consistent DLL linking.
 */
