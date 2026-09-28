package com.paiheulevrai.choochootracker;

import android.content.Intent;
import android.content.SharedPreferences;
import android.content.UriPermission;
import android.net.Uri;
import android.os.Bundle;
import android.provider.DocumentsContract;
import android.provider.OpenableColumns;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import org.libsdl.app.SDLActivity;

public final class ChooChooTrackerActivity extends SDLActivity {
    private static final int OPEN_DOCUMENT = 4101;
    private static final int CREATE_DOCUMENT = 4102;
    private static final int PICK_FOLDER = 4103;
    private static final String PREFS_NAME = "choochootracker_prefs";
    private static final String PREF_TREE_URI = "working_folder_tree_uri";
    // Size-match is only a safe "probably unchanged" signal for files too big
    // to have their size accidentally preserved across a real edit (a .cct
    // project file is text - changing one note can leave its byte size
    // exactly the same). Only skip the copy above this size, e.g. samples;
    // anything at or below is always copied, so a project file is never
    // silently skipped.
    private static final long SYNC_SIZE_SKIP_THRESHOLD = 128 * 1024;
    private String importDirectory;
    private String exportPath;
    private String workingFolderMessage = "";

    @Override protected String[] getLibraries() {
        return new String[] { "SDL2", "chipnomad" };
    }

    @Override protected String[] getArguments() {
        // OpenSL ES's fast output underruns on Pixel 7a even with a cheap sine
        // callback. Prefer SDL's buffered AAudio path; retain older-device fallback.
        nativeSetenv("SDL_AUDIODRIVER", "aaudio,openslES");
        // ADB-only experiments, before SDL initializes audio. Release builds
        // ignore these extras; no diagnostic controls enter the product UI.
        if ((getApplicationInfo().flags & android.content.pm.ApplicationInfo.FLAG_DEBUGGABLE) != 0
                && getIntent().getBooleanExtra("cct_audio_diag", false)) {
            nativeSetenv("CCT_AUDIO_DIAG", "1");
            nativeSetenv("CCT_AUDIO_TONE", getIntent().getBooleanExtra("cct_audio_tone", false) ? "1" : "0");
            String driver = getIntent().getStringExtra("cct_audio_driver");
            if ("openslES".equals(driver) || "aaudio".equals(driver))
                nativeSetenv("SDL_AUDIODRIVER", driver);
        }
        return super.getArguments();
    }

    @Override protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        hideSystemBars();
        // Pull whatever a synced working folder (e.g. Syncthing) delivered
        // since last launch - must happen before the native side starts and
        // loads the autosave, so there is nothing in memory yet to conflict
        // with (see docs/build-notes.md and the working-folder feature notes
        // for why this isn't a live-mounted folder: direct path access to a
        // SAF-granted folder is blocked by scoped storage on this device).
        pullFromWorkingFolder();
        // AssetManager lists nested directories reliably, unlike the native
        // asset API on a few Android builds. Never overwrite user files.
        seedWorkspace("choochootracker_data", getWorkspacePath());
    }

    @Override protected void onPause() {
        super.onPause();
        // By now SDLActivity has forwarded the background transition to the
        // native side, which already autosaves on it (see MainLoopEvent::sleep
        // in tracker/src/app.cpp) - push whatever is on disk now.
        new Thread(this::pushToWorkingFolder).start();
    }

    private void seedWorkspace(String assetPath, String destinationPath) {
        try {
            String[] children = getAssets().list(assetPath);
            if (children != null && children.length > 0) {
                new File(destinationPath).mkdirs();
                for (String child : children) seedWorkspace(assetPath + "/" + child,
                    destinationPath + "/" + child);
                return;
            }
            File destination = new File(destinationPath);
            if (destination.exists()) return;
            File parent = destination.getParentFile();
            if (parent != null) parent.mkdirs();
            try (InputStream input = getAssets().open(assetPath);
                 FileOutputStream output = new FileOutputStream(destination)) {
                byte[] buffer = new byte[32768];
                for (int read; (read = input.read(buffer)) != -1;) output.write(buffer, 0, read);
            }
        } catch (Exception ignored) { }
    }

    // --- Working folder sync (e.g. a Syncthing-watched folder) ---
    //
    // Direct path access to a SAF-granted folder is blocked by scoped storage
    // on this device (confirmed: EPERM even with a valid persisted grant), so
    // the internal workspace stays the only thing the native engine ever
    // touches. The chosen folder is only ever read/written through
    // ContentResolver/DocumentsContract, at two safe points: pulled in before
    // the native side starts (onCreate, nothing in memory yet to conflict
    // with), pushed out after it backgrounds and autosaves (onPause).

    private String workingFolderTreeUri() {
        return getSharedPreferences(PREFS_NAME, MODE_PRIVATE).getString(PREF_TREE_URI, null);
    }

    private boolean isWorkingFolderValid(Uri treeUri) {
        try {
            for (UriPermission perm : getContentResolver().getPersistedUriPermissions()) {
                if (perm.getUri().equals(treeUri) && perm.isReadPermission() && perm.isWritePermission()) return true;
            }
        } catch (Exception ignored) { }
        return false;
    }

    // Called by the native settings screen to show the current status.
    public String getWorkingFolderStatus() {
        String treeUriStr = workingFolderTreeUri();
        if (treeUriStr != null && isWorkingFolderValid(Uri.parse(treeUriStr))) {
            return "Syncing: " + Uri.parse(treeUriStr).getLastPathSegment();
        }
        if (!workingFolderMessage.isEmpty()) return workingFolderMessage;
        return "Default (app storage)";
    }

    public void pullFromWorkingFolder() {
        String treeUriStr = workingFolderTreeUri();
        if (treeUriStr == null) return;
        Uri treeUri = Uri.parse(treeUriStr);
        if (!isWorkingFolderValid(treeUri)) return;
        try {
            copyFromTree(treeUri, DocumentsContract.getTreeDocumentId(treeUri), new File(getFilesDir(), "workspace"));
        } catch (Exception e) {
            android.util.Log.w("CCTFolder", "pullFromWorkingFolder failed", e);
        }
    }

    public void pushToWorkingFolder() {
        String treeUriStr = workingFolderTreeUri();
        if (treeUriStr == null) return;
        Uri treeUri = Uri.parse(treeUriStr);
        if (!isWorkingFolderValid(treeUri)) return;
        try {
            copyToTree(new File(getFilesDir(), "workspace"), treeUri, DocumentsContract.getTreeDocumentId(treeUri));
        } catch (Exception e) {
            android.util.Log.w("CCTFolder", "pushToWorkingFolder failed", e);
        }
    }

    // SAF tree -> internal File, recursively. Above SYNC_SIZE_SKIP_THRESHOLD,
    // skips a file whose size already matches (avoids re-downloading unchanged
    // samples); small files (project files included) are always copied.
    // Overwriting is safe since this only ever runs before the native side
    // has started.
    private void copyFromTree(Uri treeUri, String parentDocId, File destDir) {
        destDir.mkdirs();
        Uri childrenUri = DocumentsContract.buildChildDocumentsUriUsingTree(treeUri, parentDocId);
        try (android.database.Cursor cursor = getContentResolver().query(childrenUri, new String[]{
                DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                DocumentsContract.Document.COLUMN_MIME_TYPE,
                DocumentsContract.Document.COLUMN_SIZE}, null, null, null)) {
            if (cursor == null) return;
            while (cursor.moveToNext()) {
                String docId = cursor.getString(0);
                String name = cursor.getString(1);
                String mime = cursor.getString(2);
                long size = cursor.getLong(3);
                if (name == null || docId == null) continue;
                if (DocumentsContract.Document.MIME_TYPE_DIR.equals(mime)) {
                    copyFromTree(treeUri, docId, new File(destDir, name));
                    continue;
                }
                File destFile = new File(destDir, name);
                if (size >= SYNC_SIZE_SKIP_THRESHOLD && destFile.exists() && destFile.length() == size) continue;
                Uri fileUri = DocumentsContract.buildDocumentUriUsingTree(treeUri, docId);
                try (InputStream input = getContentResolver().openInputStream(fileUri);
                     FileOutputStream output = new FileOutputStream(destFile)) {
                    byte[] buffer = new byte[32768];
                    for (int read; input != null && (read = input.read(buffer)) != -1;) output.write(buffer, 0, read);
                } catch (Exception e) {
                    android.util.Log.w("CCTFolder", "pull copy failed for " + name, e);
                }
            }
        } catch (Exception e) {
            android.util.Log.w("CCTFolder", "copyFromTree query failed", e);
        }
    }

    // Internal File -> SAF tree, recursively. Above SYNC_SIZE_SKIP_THRESHOLD,
    // skips a file whose size already matches (unchanged samples aren't
    // re-uploaded every background); small files (project files included)
    // are always copied. Overwriting is safe since this only ever runs right
    // after the native side has autosaved, so the internal copy is authoritative.
    private void copyToTree(File sourceDir, Uri treeUri, String parentDocId) {
        File[] children = sourceDir.listFiles();
        if (children == null) return;
        java.util.Map<String, String[]> existing = new java.util.HashMap<>(); // name -> {docId, mime, size}
        Uri childrenUri = DocumentsContract.buildChildDocumentsUriUsingTree(treeUri, parentDocId);
        try (android.database.Cursor cursor = getContentResolver().query(childrenUri, new String[]{
                DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                DocumentsContract.Document.COLUMN_MIME_TYPE,
                DocumentsContract.Document.COLUMN_SIZE}, null, null, null)) {
            if (cursor != null) {
                while (cursor.moveToNext()) {
                    existing.put(cursor.getString(1),
                        new String[]{cursor.getString(0), cursor.getString(2), cursor.getString(3)});
                }
            }
        } catch (Exception e) {
            android.util.Log.w("CCTFolder", "copyToTree query failed", e);
            return;
        }
        for (File child : children) {
            String[] found = existing.get(child.getName());
            if (child.isDirectory()) {
                String childDocId = (found != null && DocumentsContract.Document.MIME_TYPE_DIR.equals(found[1]))
                    ? found[0] : documentIdOrNull(createChild(treeUri, parentDocId, child.getName(), DocumentsContract.Document.MIME_TYPE_DIR));
                if (childDocId == null) continue;
                copyToTree(child, treeUri, childDocId);
                continue;
            }
            if (found != null && found[2] != null && child.length() >= SYNC_SIZE_SKIP_THRESHOLD) {
                try {
                    if (child.length() == Long.parseLong(found[2])) continue;
                } catch (NumberFormatException ignored) { }
            }
            Uri fileUri = (found != null)
                ? DocumentsContract.buildDocumentUriUsingTree(treeUri, found[0])
                : createChild(treeUri, parentDocId, child.getName(), "application/octet-stream");
            if (fileUri == null) continue;
            try (InputStream input = new java.io.FileInputStream(child);
                 java.io.OutputStream output = getContentResolver().openOutputStream(fileUri, "wt")) {
                byte[] buffer = new byte[32768];
                for (int read; output != null && (read = input.read(buffer)) != -1;) output.write(buffer, 0, read);
            } catch (Exception e) {
                android.util.Log.w("CCTFolder", "push copy failed for " + child.getName(), e);
            }
        }
    }

    private String documentIdOrNull(Uri documentUri) {
        return documentUri == null ? null : DocumentsContract.getDocumentId(documentUri);
    }

    private Uri createChild(Uri treeUri, String parentDocId, String name, String mime) {
        try {
            Uri parentUri = DocumentsContract.buildDocumentUriUsingTree(treeUri, parentDocId);
            return DocumentsContract.createDocument(getContentResolver(), parentUri, mime, name);
        } catch (Exception e) {
            android.util.Log.w("CCTFolder", "createChild failed for " + name, e);
            return null;
        }
    }

    // Real proof that this SAF tree is actually writable, done entirely
    // through ContentResolver/DocumentsContract - a plain java.io.File on a
    // path derived from the tree URI is NOT a valid test, it can return
    // EPERM even with a granted permission (scoped storage).
    private boolean verifyFolderWritableSAF(Uri treeUri) {
        try {
            String rootDocId = DocumentsContract.getTreeDocumentId(treeUri);
            Uri probe = createChild(treeUri, rootDocId, ".cct_write_test", "application/octet-stream");
            if (probe == null) return false;
            try (java.io.OutputStream out = getContentResolver().openOutputStream(probe, "wt")) {
                if (out == null) return false;
                out.write(1);
            }
            boolean ok;
            try (InputStream in = getContentResolver().openInputStream(probe)) {
                ok = in != null && in.read() != -1;
            }
            DocumentsContract.deleteDocument(getContentResolver(), probe);
            return ok;
        } catch (Exception e) {
            android.util.Log.w("CCTFolder", "verifyFolderWritableSAF failed", e);
            return false;
        }
    }

    private void hideSystemBars() {
        getWindow().getDecorView().setSystemUiVisibility(
            android.view.View.SYSTEM_UI_FLAG_FULLSCREEN |
            android.view.View.SYSTEM_UI_FLAG_HIDE_NAVIGATION |
            android.view.View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY |
            android.view.View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN |
            android.view.View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION |
            android.view.View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
    }

    @Override public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) hideSystemBars();
    }

    public void saveDocument(String path, String mimeType, String suggestedName) {
        runOnUiThread(() -> {
            exportPath = path;
            Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType(mimeType);
            intent.putExtra(Intent.EXTRA_TITLE, suggestedName);
            startActivityForResult(intent, CREATE_DOCUMENT);
        });
    }

    // Called by the native layer. Always the app-private workspace - a
    // synced working folder is never read/written directly by the engine,
    // see the sync methods above.
    public String getWorkspacePath() {
        File workspace = new File(getFilesDir(), "workspace");
        workspace.mkdirs();
        return workspace.getAbsolutePath();
    }

    // Called by the native layer to open the system folder picker.
    public void pickWorkingFolder() {
        android.util.Log.w("CCTFolder", "pickWorkingFolder() called");
        runOnUiThread(() -> {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
            intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION
                | Intent.FLAG_GRANT_WRITE_URI_PERMISSION
                | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
            startActivityForResult(intent, PICK_FOLDER);
        });
    }

    // Called by the native layer to stop syncing and revert to app-only storage.
    public void clearWorkingFolder() {
        SharedPreferences prefs = getSharedPreferences(PREFS_NAME, MODE_PRIVATE);
        String treeUriStr = prefs.getString(PREF_TREE_URI, null);
        if (treeUriStr != null) {
            try {
                Uri treeUri = Uri.parse(treeUriStr);
                getContentResolver().releasePersistableUriPermission(treeUri,
                    Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
            } catch (Exception ignored) { }
        }
        prefs.edit().remove(PREF_TREE_URI).apply();
        workingFolderMessage = "Sync stopped - back to app storage";
    }

    public void openDocument(String mimeType, String relativeDirectory) {
        runOnUiThread(() -> {
            importDirectory = relativeDirectory;
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType(mimeType);
            startActivityForResult(intent, OPEN_DOCUMENT);
        });
    }

    @Override protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        android.util.Log.w("CCTFolder", "onActivityResult requestCode=" + requestCode
            + " resultCode=" + resultCode + " data=" + (data == null ? "null" : data.getData()));
        if (resultCode != RESULT_OK || data == null || data.getData() == null) {
            if (requestCode == PICK_FOLDER) {
                workingFolderMessage = "Folder pick cancelled or failed (code " + resultCode + ")";
            }
            return;
        }
        if (requestCode == CREATE_DOCUMENT) {
            try (InputStream input = new java.io.FileInputStream(exportPath);
                 java.io.OutputStream output = getContentResolver().openOutputStream(data.getData())) {
                byte[] buffer = new byte[32768];
                for (int read; output != null && (read = input.read(buffer)) != -1;) output.write(buffer, 0, read);
            } catch (Exception ignored) { }
            return;
        }
        if (requestCode == PICK_FOLDER) {
            Uri treeUri = data.getData();
            boolean writable = verifyFolderWritableSAF(treeUri);
            android.util.Log.w("CCTFolder", "treeUri=" + treeUri + " writable=" + writable);
            if (writable) {
                try {
                    getContentResolver().takePersistableUriPermission(treeUri,
                        Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
                } catch (Exception e) {
                    android.util.Log.w("CCTFolder", "takePersistableUriPermission failed", e);
                    workingFolderMessage = "Could not persist folder access: " + e.getMessage();
                    return;
                }
                getSharedPreferences(PREFS_NAME, MODE_PRIVATE).edit()
                    .putString(PREF_TREE_URI, treeUri.toString())
                    .apply();
                workingFolderMessage = "Sync started - pushing current project now";
                new Thread(this::pushToWorkingFolder).start();
            } else {
                workingFolderMessage = "This folder can't be synced - staying on app storage";
            }
            return;
        }
        if (requestCode != OPEN_DOCUMENT) return;
        Uri uri = data.getData();
        String name = "import";
        try (android.database.Cursor cursor = getContentResolver().query(uri, null, null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) {
                int column = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                if (column >= 0) name = cursor.getString(column);
            }
        } catch (Exception ignored) { }
        name = name.replaceAll("[^A-Za-z0-9._ -]", "_");
        File destination = new File(new File(getWorkspacePath(), importDirectory), name);
        destination.getParentFile().mkdirs();
        try (InputStream input = getContentResolver().openInputStream(uri);
             FileOutputStream output = new FileOutputStream(destination)) {
            byte[] buffer = new byte[32768];
            for (int read; input != null && (read = input.read(buffer)) != -1;) output.write(buffer, 0, read);
        } catch (Exception ignored) { }
    }
}
