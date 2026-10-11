package com.paiheulevrai.choochootracker;

import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.UriPermission;
import android.provider.DocumentsContract;
import android.content.IntentFilter;
import android.hardware.usb.UsbConstants;
import android.hardware.usb.UsbDevice;
import android.hardware.usb.UsbDeviceConnection;
import android.hardware.usb.UsbEndpoint;
import android.hardware.usb.UsbInterface;
import android.hardware.usb.UsbManager;
import android.media.midi.MidiDevice;
import android.media.midi.MidiDeviceInfo;
import android.media.midi.MidiInputPort;
import android.media.midi.MidiManager;
import android.media.midi.MidiOutputPort;
import android.media.midi.MidiReceiver;
import android.net.Uri;
import android.os.Bundle;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.provider.OpenableColumns;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.IOException;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.Set;
import java.util.Locale;
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
    private static final String ACTION_USB_PERMISSION =
            "com.paiheulevrai.choochootracker.USB_PERMISSION";
    private String importDirectory;
    private String exportPath;
    private String workingFolderMessage = "";

    private MidiManager midiManager;
    private MidiManager.DeviceCallback midiDeviceCallback;
    private UsbManager usbManager;
    private Handler midiHandler;
    private final ArrayList<MidiEndpoint> midiInputs = new ArrayList<>();
    private final ArrayList<MidiEndpoint> midiOutputs = new ArrayList<>();
    private final ArrayList<UsbEndpointRecord> usbInputs = new ArrayList<>();
    private final ArrayList<UsbEndpointRecord> usbOutputs = new ArrayList<>();
    private MidiDevice openedInputDevice;
    private MidiOutputPort openedInputPort;
    private MidiDevice openedOutputDevice;
    private MidiInputPort openedOutputPort;
    private volatile int inputOpenGeneration;
    private volatile int outputOpenGeneration;
    private UsbDeviceConnection openedUsbInputConnection;
    private UsbInterface openedUsbInputInterface;
    private UsbEndpoint openedUsbInputEndpoint;
    private Thread usbInputThread;
    private UsbDeviceConnection openedUsbOutputConnection;
    private UsbEndpoint openedUsbOutputEndpoint;
    private UsbEndpointRecord pendingUsbInput;
    private UsbEndpointRecord pendingUsbOutput;
    private int pendingUsbInputGeneration;
    private int pendingUsbOutputGeneration;

    private static final class MidiEndpoint {
        final MidiDeviceInfo device;
        final int port;
        final String name;

        MidiEndpoint(MidiDeviceInfo device, int port, boolean input) {
            this.device = device;
            this.port = port;
            String deviceName = device.getProperties().getString(MidiDeviceInfo.PROPERTY_NAME);
            if (deviceName == null || deviceName.length() == 0) deviceName = "MIDI device";
            this.name = deviceName + " [" + (input ? "in " : "out ") + (port + 1) + "]";
        }
    }

    private static final class UsbEndpointRecord {
        final UsbDevice device;
        final UsbInterface usbInterface;
        final UsbEndpoint endpoint;
        final String name;

        UsbEndpointRecord(UsbDevice device, UsbInterface usbInterface,
                UsbEndpoint endpoint, int port, boolean input) {
            this.device = device;
            this.usbInterface = usbInterface;
            this.endpoint = endpoint;
            String manufacturer = device.getManufacturerName();
            String product = device.getProductName();
            String deviceName = ((manufacturer == null || manufacturer.length() == 0) ? "" : manufacturer + " ")
                    + ((product == null || product.length() == 0) ? "USB MIDI" : product);
            this.name = deviceName + " [" + (input ? "in " : "out ") + (port + 1) + "]";
        }
    }

    private final BroadcastReceiver usbReceiver = new BroadcastReceiver() {
        @Override public void onReceive(Context context, Intent intent) {
            String action = intent.getAction();
            if (ACTION_USB_PERMISSION.equals(action)) {
                UsbDevice device = getUsbDevice(intent);
                boolean granted = intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false);
                synchronized (ChooChooTrackerActivity.this) {
                    if (device != null && granted) {
                        if (pendingUsbInput != null && pendingUsbInput.device.getDeviceName().equals(device.getDeviceName())) {
                            UsbEndpointRecord record = findUsbEndpoint(usbInputs, device.getDeviceName(), pendingUsbInput.endpoint.getAddress());
                            int generation = pendingUsbInputGeneration;
                            pendingUsbInput = null;
                            if (record != null && generation == inputOpenGeneration) startUsbInput(record, generation);
                        }
                        if (pendingUsbOutput != null && pendingUsbOutput.device.getDeviceName().equals(device.getDeviceName())) {
                            UsbEndpointRecord record = findUsbEndpoint(usbOutputs, device.getDeviceName(), pendingUsbOutput.endpoint.getAddress());
                            int generation = pendingUsbOutputGeneration;
                            pendingUsbOutput = null;
                            if (record != null && generation == outputOpenGeneration) startUsbOutput(record, generation);
                        }
                    } else {
                        pendingUsbInput = null;
                        pendingUsbOutput = null;
                    }
                    rebuildMidiEndpoints();
                }
                return;
            }
            if (UsbManager.ACTION_USB_DEVICE_ATTACHED.equals(action)
                    || UsbManager.ACTION_USB_DEVICE_DETACHED.equals(action)) {
                synchronized (ChooChooTrackerActivity.this) {
                    closeMidiInput();
                    closeMidiOutput();
                    rebuildMidiEndpoints();
                }
            }
        }
    };

    private final MidiReceiver midiReceiver = new MidiReceiver() {
        @Override public void onSend(byte[] data, int offset, int count, long timestamp) {
            if (data == null || offset < 0 || count <= 0 || offset + count > data.length) return;
            byte[] copy = new byte[count];
            System.arraycopy(data, offset, copy, 0, count);
            nativeMidiMessage(copy, timestamp);
        }
    };

    public static native void nativeMidiMessage(byte[] data, long timestamp);

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

    // Called by the native backend after SDL has loaded the application.
    public synchronized void initializeMidi() {
        if (midiManager != null) return;
        midiManager = (MidiManager)getSystemService(MIDI_SERVICE);
        if (midiManager == null) return;
        usbManager = (UsbManager)getSystemService(Context.USB_SERVICE);
        midiHandler = new Handler(Looper.getMainLooper());
        registerUsbReceiver();
        rebuildMidiEndpoints();
        Log.i("ChooChooTracker", "MIDI initialized: " + midiInputs.size() + " input, "
                + midiOutputs.size() + " output port(s)");
        midiDeviceCallback = new MidiManager.DeviceCallback() {
            @Override public void onDeviceAdded(MidiDeviceInfo info) { rebuildMidiEndpoints(); }
            @Override public void onDeviceRemoved(MidiDeviceInfo info) {
                closeMidiInput();
                closeMidiOutput();
                rebuildMidiEndpoints();
            }
        };
        midiManager.registerDeviceCallback(midiDeviceCallback, midiHandler);
    }

    private synchronized void rebuildMidiEndpoints() {
        midiInputs.clear();
        midiOutputs.clear();
        usbInputs.clear();
        usbOutputs.clear();
        if (midiManager == null) return;
        try {
            Set<String> midiManagerUsbDevicesWithPorts = new HashSet<>();
            for (MidiDeviceInfo device : midiManager.getDevices()) {
                int inputPorts = device.getOutputPortCount();
                int outputPorts = device.getInputPortCount();
                for (int port = 0; port < inputPorts; ++port)
                    midiInputs.add(new MidiEndpoint(device, port, true));
                for (int port = 0; port < outputPorts; ++port)
                    midiOutputs.add(new MidiEndpoint(device, port, false));
                UsbDevice usbDevice = midiDeviceUsbDevice(device);
                if (usbDevice != null && (inputPorts > 0 || outputPorts > 0))
                    midiManagerUsbDevicesWithPorts.add(usbDevice.getDeviceName());
            }
            if (usbManager != null) {
                for (UsbDevice device : usbManager.getDeviceList().values()) {
                    if (midiManagerUsbDevicesWithPorts.contains(device.getDeviceName())) continue;
                    for (int i = 0; i < device.getInterfaceCount(); ++i) {
                        UsbInterface usbInterface = device.getInterface(i);
                        if (usbInterface.getInterfaceClass() != UsbConstants.USB_CLASS_AUDIO
                                || usbInterface.getInterfaceSubclass() != 3) continue;
                        int inputPort = 0;
                        int outputPort = 0;
                        for (int e = 0; e < usbInterface.getEndpointCount(); ++e) {
                            UsbEndpoint endpoint = usbInterface.getEndpoint(e);
                            if (endpoint.getType() != UsbConstants.USB_ENDPOINT_XFER_BULK) continue;
                            if (endpoint.getDirection() == UsbConstants.USB_DIR_IN)
                                usbInputs.add(new UsbEndpointRecord(device, usbInterface, endpoint, inputPort++, true));
                            else if (endpoint.getDirection() == UsbConstants.USB_DIR_OUT)
                                usbOutputs.add(new UsbEndpointRecord(device, usbInterface, endpoint, outputPort++, false));
                        }
                    }
                }
            }
            Log.i("ChooChooTracker", "MIDI ports refreshed: " + midiInputs.size() + " input, "
                    + midiOutputs.size() + " output port(s), USB fallback: " + usbInputs.size()
                    + " input, " + usbOutputs.size() + " output port(s)");
        } catch (RuntimeException ignored) {
            Log.w("ChooChooTracker", "Unable to enumerate MIDI devices", ignored);
        }
    }

    private static UsbDevice midiDeviceUsbDevice(MidiDeviceInfo device) {
        Object value = device.getProperties().get("usb_device");
        return value instanceof UsbDevice ? (UsbDevice)value : null;
    }

    private static UsbEndpointRecord findUsbEndpoint(ArrayList<UsbEndpointRecord> records,
            String deviceName, int address) {
        for (UsbEndpointRecord record : records)
            if (record.device.getDeviceName().equals(deviceName)
                    && record.endpoint.getAddress() == address) return record;
        return null;
    }

    @SuppressWarnings("deprecation")
    private static UsbDevice getUsbDevice(Intent intent) {
        return (UsbDevice)intent.getParcelableExtra(UsbManager.EXTRA_DEVICE);
    }

    private void registerUsbReceiver() {
        IntentFilter filter = new IntentFilter();
        filter.addAction(ACTION_USB_PERMISSION);
        filter.addAction(UsbManager.ACTION_USB_DEVICE_ATTACHED);
        filter.addAction(UsbManager.ACTION_USB_DEVICE_DETACHED);
        if (Build.VERSION.SDK_INT >= 33)
            registerReceiver(usbReceiver, filter, Context.RECEIVER_EXPORTED);
        else registerReceiver(usbReceiver, filter);
    }

    private void requestUsbPermission(UsbDevice device) {
        if (usbManager == null) return;
        int flags = PendingIntent.FLAG_UPDATE_CURRENT;
        if (Build.VERSION.SDK_INT >= 31) flags |= PendingIntent.FLAG_MUTABLE;
        Intent intent = new Intent(ACTION_USB_PERMISSION).setPackage(getPackageName());
        PendingIntent permission = PendingIntent.getBroadcast(this, device.getDeviceId(), intent, flags);
        usbManager.requestPermission(device, permission);
    }

    public synchronized int midiInputPortCount() {
        initializeMidi();
        return midiInputs.size() + usbInputs.size();
    }
    public synchronized int midiOutputPortCount() {
        initializeMidi();
        return midiOutputs.size() + usbOutputs.size();
    }
    public synchronized String midiInputPortName(int index) {
        initializeMidi();
        if (index >= 0 && index < midiInputs.size()) return midiInputs.get(index).name;
        index -= midiInputs.size();
        return index >= 0 && index < usbInputs.size() ? usbInputs.get(index).name : null;
    }
    public synchronized String midiOutputPortName(int index) {
        initializeMidi();
        if (index >= 0 && index < midiOutputs.size()) return midiOutputs.get(index).name;
        index -= midiOutputs.size();
        return index >= 0 && index < usbOutputs.size() ? usbOutputs.get(index).name : null;
    }

    public synchronized boolean openMidiInput(int index) {
        initializeMidi();
        closeMidiInput();
        if (index >= midiInputs.size()) {
            index -= midiInputs.size();
            if (index < 0 || index >= usbInputs.size()) return false;
            final int generation = ++inputOpenGeneration;
            final UsbEndpointRecord endpoint = usbInputs.get(index);
            if (usbManager.hasPermission(endpoint.device)) startUsbInput(endpoint, generation);
            else {
                pendingUsbInput = endpoint;
                pendingUsbInputGeneration = generation;
                requestUsbPermission(endpoint.device);
            }
            return true;
        }
        if (index < 0) return false;
        final int generation = ++inputOpenGeneration;
        final MidiEndpoint endpoint = midiInputs.get(index);
        midiManager.openDevice(endpoint.device, device -> {
            synchronized (ChooChooTrackerActivity.this) {
                if (generation != inputOpenGeneration || device == null) {
                    if (device != null) try { device.close(); } catch (IOException ignored) { }
                    return;
                }
                try {
                    MidiOutputPort port = device.openOutputPort(endpoint.port);
                    if (port == null) {
                        if (port != null) port.close();
                        device.close();
                        return;
                    }
                    port.connect(midiReceiver);
                    openedInputDevice = device;
                    openedInputPort = port;
                } catch (IOException ignored) {
                    try { device.close(); } catch (IOException ignoredAgain) { }
                }
            }
        }, midiHandler);
        return true;
    }

    public synchronized void closeMidiInput() {
        ++inputOpenGeneration;
        pendingUsbInput = null;
        if (openedInputPort != null) try { openedInputPort.close(); } catch (IOException ignored) { }
        if (openedInputDevice != null) try { openedInputDevice.close(); } catch (IOException ignored) { }
        openedInputPort = null;
        openedInputDevice = null;
        Thread inputThread = usbInputThread;
        usbInputThread = null;
        if (inputThread != null) inputThread.interrupt();
        UsbDeviceConnection inputConnection = openedUsbInputConnection;
        UsbInterface inputInterface = openedUsbInputInterface;
        openedUsbInputConnection = null;
        openedUsbInputInterface = null;
        if (inputConnection != null) {
            if (inputInterface != null) inputConnection.releaseInterface(inputInterface);
            inputConnection.close();
        }
        if (inputThread != null && inputThread != Thread.currentThread()) {
            try { inputThread.join(200); } catch (InterruptedException ignored) { Thread.currentThread().interrupt(); }
        }
        openedUsbInputEndpoint = null;
    }

    public synchronized boolean openMidiOutput(int index) {
        initializeMidi();
        closeMidiOutput();
        if (index >= midiOutputs.size()) {
            index -= midiOutputs.size();
            if (index < 0 || index >= usbOutputs.size()) return false;
            final int generation = ++outputOpenGeneration;
            final UsbEndpointRecord endpoint = usbOutputs.get(index);
            if (usbManager.hasPermission(endpoint.device)) startUsbOutput(endpoint, generation);
            else {
                pendingUsbOutput = endpoint;
                pendingUsbOutputGeneration = generation;
                requestUsbPermission(endpoint.device);
            }
            return true;
        }
        if (index < 0) return false;
        final int generation = ++outputOpenGeneration;
        final MidiEndpoint endpoint = midiOutputs.get(index);
        midiManager.openDevice(endpoint.device, device -> {
            synchronized (ChooChooTrackerActivity.this) {
                if (generation != outputOpenGeneration || device == null) {
                    if (device != null) try { device.close(); } catch (IOException ignored) { }
                    return;
                }
                try {
                    MidiInputPort port = device.openInputPort(endpoint.port);
                    if (port == null) {
                        device.close();
                        return;
                    }
                    openedOutputDevice = device;
                    openedOutputPort = port;
                } catch (IOException ignored) {
                    try { device.close(); } catch (IOException ignoredAgain) { }
                }
            }
        }, midiHandler);
        return true;
    }

    public synchronized void closeMidiOutput() {
        ++outputOpenGeneration;
        pendingUsbOutput = null;
        if (openedOutputPort != null) try { openedOutputPort.close(); } catch (IOException ignored) { }
        if (openedOutputDevice != null) try { openedOutputDevice.close(); } catch (IOException ignored) { }
        openedOutputPort = null;
        openedOutputDevice = null;
        if (openedUsbOutputConnection != null) openedUsbOutputConnection.close();
        openedUsbOutputConnection = null;
        openedUsbOutputEndpoint = null;
    }

    public synchronized void sendMidiOutput(byte[] data) {
        if (data == null) return;
        if (openedOutputPort != null) {
            try { openedOutputPort.send(data, 0, data.length, 0); }
            catch (IOException ignored) { }
            return;
        }
        if (openedUsbOutputConnection == null || openedUsbOutputEndpoint == null || data.length == 0) return;
        int status = data[0] & 0xff;
        int high = status & 0xf0;
        int length = (high == 0xc0 || high == 0xd0) ? 2 : 3;
        int cin = high == 0xc0 || high == 0xd0 ? (high >> 4) : (high >> 4);
        if (status >= 0xf0) {
            if (status == 0xf1 || status == 0xf3) { cin = 2; length = 2; }
            else if (status == 0xf2) { cin = 3; length = 3; }
            else { cin = 5; length = 1; }
        }
        byte[] packet = new byte[] {(byte)cin, data[0],
                length > 1 && data.length > 1 ? data[1] : 0,
                length > 2 && data.length > 2 ? data[2] : 0};
        openedUsbOutputConnection.bulkTransfer(openedUsbOutputEndpoint, packet, packet.length, 50);
    }

    private void startUsbInput(UsbEndpointRecord record, int generation) {
        if (usbManager == null || generation != inputOpenGeneration) return;
        UsbDeviceConnection connection = usbManager.openDevice(record.device);
        if (connection == null || !connection.claimInterface(record.usbInterface, true)) {
            if (connection != null) connection.close();
            return;
        }
        openedUsbInputConnection = connection;
        openedUsbInputInterface = record.usbInterface;
        openedUsbInputEndpoint = record.endpoint;
        usbInputThread = new Thread(() -> readUsbInput(connection, record.endpoint, generation), "cct-usb-midi-in");
        usbInputThread.start();
    }

    private void readUsbInput(UsbDeviceConnection connection, UsbEndpoint endpoint, int generation) {
        byte[] buffer = new byte[Math.max(64, endpoint.getMaxPacketSize())];
        int pending = 0;
        while (!Thread.currentThread().isInterrupted() && generation == inputOpenGeneration) {
            int read = connection.bulkTransfer(endpoint, buffer, pending, buffer.length - pending, 50);
            if (read <= 0) continue;
            pending += read;
            int complete = pending - (pending % 4);
            for (int offset = 0; offset < complete; offset += 4) {
                int cin = buffer[offset] & 0x0f;
                int length = usbPacketLength(cin);
                if (length <= 0) continue;
                byte[] message = new byte[length];
                System.arraycopy(buffer, offset + 1, message, 0, length);
                nativeMidiMessage(message, 0);
            }
            int remaining = pending - complete;
            if (remaining > 0) System.arraycopy(buffer, complete, buffer, 0, remaining);
            pending = remaining;
        }
    }

    private void startUsbOutput(UsbEndpointRecord record, int generation) {
        if (usbManager == null || generation != outputOpenGeneration) return;
        UsbDeviceConnection connection = usbManager.openDevice(record.device);
        if (connection == null || !connection.claimInterface(record.usbInterface, false)) {
            if (connection != null) connection.close();
            return;
        }
        openedUsbOutputConnection = connection;
        openedUsbOutputEndpoint = record.endpoint;
    }

    private static int usbPacketLength(int cin) {
        if (cin == 1 || cin == 5) return 1;
        if (cin == 2 || cin == 6 || cin == 0xc) return 2;
        if (cin == 3 || cin == 4 || cin == 7 || (cin >= 8 && cin <= 0xb) || cin == 0xe) return 3;
        if (cin == 0xd) return 2;
        return 0;
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

    @Override protected void onResume() {
        super.onResume();
        if (midiManager != null) rebuildMidiEndpoints();
    }

    @Override protected void onDestroy() {
        synchronized (this) {
            closeMidiInput();
            closeMidiOutput();
            if (midiManager != null && midiDeviceCallback != null)
                midiManager.unregisterDeviceCallback(midiDeviceCallback);
            try { unregisterReceiver(usbReceiver); } catch (IllegalArgumentException ignored) { }
        }
        super.onDestroy();
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
            // Downloads providers do not consistently label WAVs as audio/wav
            // (some use audio/x-wav or application/octet-stream). Let the user
            // choose the file, then validate its extension below.
            intent.setType("*/*");
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
        String expectedExtension = "samples".equals(importDirectory) ? ".wav" : ".cct";
        if (!name.toLowerCase(Locale.ROOT).endsWith(expectedExtension)) {
            android.widget.Toast.makeText(this, "Choose a " + expectedExtension + " file", android.widget.Toast.LENGTH_SHORT).show();
            return;
        }
        File destination = new File(new File(getWorkspacePath(), importDirectory), name);
        destination.getParentFile().mkdirs();
        try (InputStream input = getContentResolver().openInputStream(uri);
             FileOutputStream output = new FileOutputStream(destination)) {
            byte[] buffer = new byte[32768];
            for (int read; input != null && (read = input.read(buffer)) != -1;) output.write(buffer, 0, read);
        } catch (Exception ignored) { }
    }
}
