#ifndef CORELIB_FILE_H
#define CORELIB_FILE_H

#ifdef __cplusplus
extern "C" {
#endif

// Platform-specific file and directory utilities
// For general file I/O, use standard stdio.h functions

#ifdef _WIN32
#define PATH_SEPARATOR '\\'
#define PATH_SEPARATOR_STR "\\"
#else
#define PATH_SEPARATOR '/'
#define PATH_SEPARATOR_STR "/"
#endif

// Directory entry structure
struct FileEntry {
  char name[256];
  int isDirectory;
};

// Get platform-specific default directory for ChipNomad files
// Returns 0 on success, -1 on failure
int fileGetDefaultDirectory(char* buffer, int bufferSize);

// Opens Android's system document picker and copies the selected file into
// the private workspace. Other targets intentionally do nothing.
void fileImportDocument(const char* mimeType, const char* relativeDirectory);
void fileExportDocument(const char* path, const char* mimeType);

// Android only: lets the user pick a folder (Storage Access Framework) to use
// as the working directory instead of the app's private storage, with a
// persisted, revocable grant. Other targets intentionally do nothing.
void filePickWorkingFolder(void);
// Reverts to the app's private storage. Other targets intentionally do nothing.
void fileClearWorkingFolder(void);
// Fills buffer with a short human-readable status (active custom path, the
// last pick attempt's outcome, or "Default (app storage)"). Returns 0 on
// success. Other targets report "Default" without querying anything.
int fileGetWorkingFolderStatus(char* buffer, int bufferSize);

// Check if a directory exists
// Returns 1 if exists, 0 if not
int fileDirectoryExists(const char* path);

// Create a directory
// Returns 0 on success, -1 on failure
int fileCreateDirectory(const char* path);

// Delete a file
// Returns 0 on success, -1 on failure
int fileDelete(const char* path);

// List directory contents with optional extension filter
// Returns array of FileEntry (caller must free), or NULL on error
// entryCount is set to number of entries
FileEntry* fileListDirectory(const char* path, const char* extension, int* entryCount);

#ifdef __cplusplus
}
#endif

#endif // CORELIB_FILE_H
