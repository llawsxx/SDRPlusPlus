package org.sdrpp.sdrpp;

import android.app.NativeActivity;
import android.app.AlertDialog;
import android.app.PendingIntent;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.DialogInterface;
import android.content.pm.PackageManager;
import android.content.pm.ActivityInfo;
import android.content.res.Configuration;
import android.hardware.usb.*;
import android.Manifest;
import android.graphics.Color;
import android.os.Bundle;
import android.os.Build;
import android.net.Uri;
import android.provider.DocumentsContract;
import android.view.KeyEvent;
import android.view.View;
import android.view.WindowManager;
import android.view.inputmethod.InputMethodManager;
import android.util.Log;
import android.content.res.AssetManager;

import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;
import androidx.core.view.ViewCompat;
import androidx.core.view.WindowCompat;
import androidx.core.view.WindowInsetsCompat;
import androidx.core.view.WindowInsetsControllerCompat;

import androidx.core.content.PermissionChecker;

import java.util.concurrent.LinkedBlockingQueue;
import java.io.*;

private const val ACTION_USB_PERMISSION = "org.sdrpp.sdrpp.USB_PERMISSION";
private const val RECORDING_DIRECTORY_REQUEST = 7101;

class MainActivity : NativeActivity() {
    private var usbReceiverRegistered = false;
    private val requestedUsbDevices = mutableSetOf<String>();
    private val supportedUsbDevices by lazy {
        val devices = mutableSetOf<Pair<Int, Int>>();
        resources.getXml(R.xml.usb_device_filter).use { parser ->
            while (parser.eventType != org.xmlpull.v1.XmlPullParser.END_DOCUMENT) {
                if (parser.eventType == org.xmlpull.v1.XmlPullParser.START_TAG && parser.name == "usb-device") {
                    devices.add(Pair(parser.getAttributeIntValue(null, "vendor-id", -1), parser.getAttributeIntValue(null, "product-id", -1)));
                }
                parser.next();
            }
        }
        devices;
    };
    private val usbReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            val device = intent.getParcelableExtra<UsbDevice>(UsbManager.EXTRA_DEVICE) ?: return;
            when (intent.action) {
                UsbManager.ACTION_USB_DEVICE_ATTACHED -> requestUsbPermission(device);
                UsbManager.ACTION_USB_DEVICE_DETACHED -> {
                    requestedUsbDevices.remove(device.deviceName);
                    if (SDR_device?.deviceName == device.deviceName) { closeUsbDevice(); }
                }
                ACTION_USB_PERMISSION -> {
                    if (intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false) && usbManager?.hasPermission(device) == true) {
                        openUsbDevice(device);
                    } else { Log.w(TAG, "USB permission denied: ${device.deviceName}"); }
                    hideSystemBars();
                }
            }
        }
    };

    private fun requestUsbPermission(device: UsbDevice) {
        val manager = usbManager ?: return;
        if (Pair(device.vendorId, device.productId) !in supportedUsbDevices) { return; }
        if (manager.hasPermission(device)) { openUsbDevice(device); return; }
        if (!requestedUsbDevices.add(device.deviceName)) { return; }
        val flags = PendingIntent.FLAG_UPDATE_CURRENT or if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) PendingIntent.FLAG_MUTABLE else 0;
        val permissionIntent = PendingIntent.getBroadcast(this, device.deviceId,
            Intent(ACTION_USB_PERMISSION).setPackage(packageName), flags);
        try { manager.requestPermission(device, permissionIntent); }
        catch (error: Exception) {
            requestedUsbDevices.remove(device.deviceName);
            Log.e(TAG, "Cannot request USB permission", error);
        }
    }

    private fun openUsbDevice(device: UsbDevice) {
        if (SDR_device?.deviceName == device.deviceName && SDR_conn != null) { return; }
        val manager = usbManager ?: return;
        if (manager.deviceList[device.deviceName] == null || !manager.hasPermission(device)) { return; }
        if (SDR_conn != null) {
            Log.i(TAG, "Keeping the current USB device; another device was authorized: ${device.deviceName}");
            return;
        }
        try {
            val connection = manager.openDevice(device) ?: run { Log.e(TAG, "Cannot open USB device: ${device.deviceName}"); return; };
            SDR_device = device;
            SDR_conn = connection;
            SDR_VID = device.vendorId;
            SDR_PID = device.productId;
            SDR_FD = connection.fileDescriptor;
            Log.i(TAG, "USB device ready: ${device.deviceName}");
        } catch (error: Exception) { Log.e(TAG, "Cannot open USB device", error); }
    }

    private fun closeUsbDevice() {
        SDR_FD = -1;
        SDR_VID = -1;
        SDR_PID = -1;
        SDR_conn?.close();
        SDR_conn = null;
        SDR_device = null;
    }

    @Volatile private var sessionLocked = false;
    @Volatile private var screenOrientation = 0;
    @Volatile private var keepScreenOn = false;
    @Volatile private var recordingDirectoryResult = "";
    @Volatile private var recordingError = "";

    fun setSessionLocked(locked: Boolean) {
        sessionLocked = locked;
        runOnUiThread {
            updateScreenOrientation();
            updateKeepScreenOn();
        }
    }

    fun setKeepScreenOn(enabled: Boolean) {
        keepScreenOn = enabled;
        runOnUiThread { updateKeepScreenOn(); }
    }

    private fun updateKeepScreenOn() {
        if (sessionLocked || keepScreenOn) {
            window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        } else {
            window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        }
    }

    fun setScreenOrientation(orientation: Int) {
        if (sessionLocked || orientation !in 0..2) { return; }
        screenOrientation = orientation;
        runOnUiThread { updateScreenOrientation(); }
    }

    private fun updateScreenOrientation() {
        val orientation = if (sessionLocked) ActivityInfo.SCREEN_ORIENTATION_LOCKED else when (screenOrientation) {
            1 -> ActivityInfo.SCREEN_ORIENTATION_PORTRAIT;
            2 -> ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE;
            else -> ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED;
        };
        if (requestedOrientation != orientation) { requestedOrientation = orientation; }
    }

    override fun onBackPressed() {
        if (!sessionLocked) { super.onBackPressed(); }
    }

    fun chooseRecordingDirectory() {
        recordingDirectoryResult = "";
        runOnUiThread {
            try {
                val intent = Intent(Intent.ACTION_OPEN_DOCUMENT_TREE).apply {
                    addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION or Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
                    getPreferences(MODE_PRIVATE).getString("recordingTree", null)?.let {
                        putExtra(DocumentsContract.EXTRA_INITIAL_URI, Uri.parse(it));
                    }
                };
                startActivityForResult(intent, RECORDING_DIRECTORY_REQUEST);
            } catch (error: Exception) {
                recordingDirectoryResult = "error: " + (error.message ?: error.javaClass.simpleName);
            }
        }
    }

    fun getRecordingDirectoryResult(): String = recordingDirectoryResult;
    fun getRecordingError(): String = recordingError;

    fun canWriteRecordingDirectory(tree: String): Boolean {
        return try {
            val uri = Uri.parse(tree);
            contentResolver.persistedUriPermissions.any { it.uri == uri && it.isWritePermission };
        } catch (error: Exception) {
            false;
        }
    }

    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != RECORDING_DIRECTORY_REQUEST) { return; }
        if (resultCode != RESULT_OK || data?.data == null) {
            recordingDirectoryResult = "cancelled";
            return;
        }
        try {
            val uri = data.data!!;
            val flags = data.flags and (Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
            contentResolver.takePersistableUriPermission(uri, flags);
            if (!canWriteRecordingDirectory(uri.toString())) { throw IOException("Directory is not writable"); }
            getPreferences(MODE_PRIVATE).edit().putString("recordingTree", uri.toString()).apply();
            recordingDirectoryResult = uri.toString();
        } catch (error: Exception) {
            recordingDirectoryResult = "error: " + (error.message ?: error.javaClass.simpleName);
        }
    }

    fun openRecordingDocument(tree: String, filename: String): Int {
        recordingError = "";
        return try {
            val treeUri = Uri.parse(tree);
            val parent = DocumentsContract.buildDocumentUriUsingTree(treeUri, DocumentsContract.getTreeDocumentId(treeUri));
            val document = DocumentsContract.createDocument(contentResolver, parent, "audio/wav", filename)
                ?: throw IOException("Could not create recording file");
            try {
                val descriptor = contentResolver.openFileDescriptor(document, "rw")
                    ?: throw IOException("Could not open recording file");
                descriptor.use { it.detachFd() };
            } catch (error: Exception) {
                DocumentsContract.deleteDocument(contentResolver, document);
                throw error;
            }
        } catch (error: Exception) {
            recordingError = error.message ?: error.javaClass.simpleName;
            -1;
        }
    }

    private val TAG : String = "SDR++";
    public var usbManager : UsbManager? = null;
    public var SDR_device : UsbDevice? = null;
    public var SDR_conn : UsbDeviceConnection? = null;
    public var SDR_VID : Int = -1;
    public var SDR_PID : Int = -1;
    public var SDR_FD : Int = -1;

    fun checkAndAsk(permission: String) {
        if (PermissionChecker.checkSelfPermission(this, permission) != PackageManager.PERMISSION_GRANTED) {
            ActivityCompat.requestPermissions(this, arrayOf(permission), 1);
        }
    }

    public fun hideSystemBars() {
        val isLandscape = resources.configuration.orientation == Configuration.ORIENTATION_LANDSCAPE;
        if (isLandscape) {
            window.addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN);
        } else {
            window.clearFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN);
        }
        WindowCompat.setDecorFitsSystemWindows(window, Build.VERSION.SDK_INT < 35);
        window.attributes = window.attributes.apply {
            layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_NEVER;
        };
        WindowCompat.getInsetsController(window, window.decorView).apply {
            systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE;
            isAppearanceLightStatusBars = false;
            if (isLandscape) {
                hide(WindowInsetsCompat.Type.statusBars());
            } else {
                show(WindowInsetsCompat.Type.statusBars());
            }
            hide(WindowInsetsCompat.Type.navigationBars());
        };
    }

    private fun configureWindowInsets() {
        if (Build.VERSION.SDK_INT < 35) { return; }
        val content = findViewById<View>(android.R.id.content);
        content.setBackgroundColor(Color.BLACK);
        ViewCompat.setOnApplyWindowInsetsListener(content) { view, windowInsets ->
            val safeArea = windowInsets.getInsets(WindowInsetsCompat.Type.systemBars() or
                WindowInsetsCompat.Type.displayCutout() or WindowInsetsCompat.Type.ime());
            view.setPadding(safeArea.left, safeArea.top, safeArea.right, safeArea.bottom);
            WindowInsetsCompat.CONSUMED;
        };
        ViewCompat.requestApplyInsets(content);
    }

    public override fun onCreate(savedInstanceState: Bundle?) {
        // Hide bars
        hideSystemBars();

        if (Build.VERSION.SDK_INT <= Build.VERSION_CODES.P) {
            checkAndAsk(Manifest.permission.WRITE_EXTERNAL_STORAGE);
        }
        if (Build.VERSION.SDK_INT <= Build.VERSION_CODES.S_V2) {
            checkAndAsk(Manifest.permission.READ_EXTERNAL_STORAGE);
        }

        // Register events
        usbManager = getSystemService(Context.USB_SERVICE) as UsbManager;
        val filter = IntentFilter(ACTION_USB_PERMISSION).apply {
            addAction(UsbManager.ACTION_USB_DEVICE_ATTACHED);
            addAction(UsbManager.ACTION_USB_DEVICE_DETACHED);
        };
        ContextCompat.registerReceiver(this, usbReceiver, filter, ContextCompat.RECEIVER_NOT_EXPORTED);
        usbReceiverRegistered = true;

        // Get permission for all USB devices
        val devList = usbManager!!.getDeviceList();
        for (device in devList.values) { requestUsbPermission(device); }

        super.onCreate(savedInstanceState)
        configureWindowInsets();
        hideSystemBars();
    }

    public override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        if (intent.action == UsbManager.ACTION_USB_DEVICE_ATTACHED) {
            intent.getParcelableExtra<UsbDevice>(UsbManager.EXTRA_DEVICE)?.let { requestUsbPermission(it); }
        }
    }

    public override fun onDestroy() {
        if (usbReceiverRegistered) { unregisterReceiver(usbReceiver); usbReceiverRegistered = false; }
        super.onDestroy();
        closeUsbDevice();
    }

    public override fun onResume() {
        super.onResume();
        // Hide bars again
        hideSystemBars();
    }

    public override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            hideSystemBars();
        }
    }

    public override fun onConfigurationChanged(newConfig: Configuration) {
        super.onConfigurationChanged(newConfig);
        hideSystemBars();
    }

    fun showSoftInput() {
        val inputMethodManager = getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager;
        inputMethodManager.showSoftInput(window.decorView, 0);
    }

    fun hideSoftInput() {
        val inputMethodManager = getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager;
        inputMethodManager.hideSoftInputFromWindow(window.decorView.windowToken, 0);
        hideSystemBars();
    }

    // Queue for the Unicode characters to be polled from native code (via pollUnicodeChar())
    private var unicodeCharacterQueue: LinkedBlockingQueue<Int> = LinkedBlockingQueue()

    // We assume dispatchKeyEvent() of the NativeActivity is actually called for every
    // KeyEvent and not consumed by any View before it reaches here
    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        if (event.action == KeyEvent.ACTION_DOWN) {
            unicodeCharacterQueue.offer(event.getUnicodeChar(event.metaState))
        }
        return super.dispatchKeyEvent(event)
    }

    fun pollUnicodeChar(): Int {
        return unicodeCharacterQueue.poll() ?: 0
    }

    public fun createIfDoesntExist(path: String) {
        // This is a directory, create it in the filesystem
        var folder = File(path);
        var success = true;
        if (!folder.exists()) {
            success = folder.mkdirs();
        }
        if (!success) {
            Log.e(TAG, "Could not create folder with path " + path);
        }
    }

    public fun extractDir(aman: AssetManager, local: String, rsrc: String): Int {
        val flist = aman.list(rsrc) ?: emptyArray();
        var ecount = 0;
        for (fp in flist) {
            val lpath = local + "/" + fp;
            val rpath = rsrc + "/" + fp;

            Log.w(TAG, "Extracting '" + rpath + "' to '" + lpath + "'");

            // Create local path if non-existent
            createIfDoesntExist(local);
            
            // Create if directory
            val ext = extractDir(aman, lpath, rpath);

            // Extract if file
            if (ext == 0) {
                // This is a file, extract it
                val _os = FileOutputStream(lpath);
                val _is = aman.open(rpath);
                val ilen = _is.available();
                var fbuf = ByteArray(ilen);
                _is.read(fbuf, 0, ilen);
                _os.write(fbuf);
                _os.close();
                _is.close();
            }

            ecount++;
        }
        return ecount;
    }

    public fun getAppDir(): String {
        val fdir = getFilesDir().getAbsolutePath();

        // Extract all resources to the app directory
        val aman = getAssets();
        extractDir(aman, fdir + "/res", "res");
        createIfDoesntExist(fdir + "/modules");

        return fdir;
    }
}
