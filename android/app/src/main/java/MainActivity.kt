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
import android.os.Bundle;
import android.net.Uri;
import android.provider.DocumentsContract;
import android.view.KeyEvent;
import android.view.WindowManager;
import android.view.inputmethod.InputMethodManager;
import android.util.Log;
import android.content.res.AssetManager;

import androidx.core.app.ActivityCompat;
import androidx.core.view.WindowCompat;
import androidx.core.view.WindowInsetsCompat;
import androidx.core.view.WindowInsetsControllerCompat;

import androidx.core.content.PermissionChecker;

import java.util.concurrent.LinkedBlockingQueue;
import java.io.*;

private const val ACTION_USB_PERMISSION = "org.sdrpp.sdrpp.USB_PERMISSION";
private const val RECORDING_DIRECTORY_REQUEST = 7101;

private val usbReceiver = object : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        if (ACTION_USB_PERMISSION == intent.action) {
            synchronized(this) {
                var _this = context as MainActivity;
                _this.SDR_device = intent.getParcelableExtra(UsbManager.EXTRA_DEVICE)
                if (intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false)) {
                    _this.SDR_conn = _this.usbManager!!.openDevice(_this.SDR_device);
                    
                    // Save SDR info
                    _this.SDR_VID = _this.SDR_device!!.getVendorId();
                    _this.SDR_PID = _this.SDR_device!!.getProductId()
                    _this.SDR_FD = _this.SDR_conn!!.getFileDescriptor();
                }
                
                // Whatever the hell this does
                context.unregisterReceiver(this);

                // Hide again the system bars
                _this.hideSystemBars();
            }
        }
    }
}

class MainActivity : NativeActivity() {
    @Volatile private var sessionLocked = false;
    @Volatile private var recordingDirectoryResult = "";
    @Volatile private var recordingError = "";

    fun setSessionLocked(locked: Boolean) {
        sessionLocked = locked;
        runOnUiThread {
            requestedOrientation = if (locked) ActivityInfo.SCREEN_ORIENTATION_LOCKED else ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED;
            if (locked) {
                window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
            } else {
                window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
            }
        }
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
        WindowCompat.setDecorFitsSystemWindows(window, true);
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

    public override fun onCreate(savedInstanceState: Bundle?) {
        // Hide bars
        hideSystemBars();

        // Ask for required permissions, without these the app cannot run.
        checkAndAsk(Manifest.permission.WRITE_EXTERNAL_STORAGE);
        checkAndAsk(Manifest.permission.READ_EXTERNAL_STORAGE);

        // TODO: Have the main code wait until these two permissions are available

        // Register events
        usbManager = getSystemService(Context.USB_SERVICE) as UsbManager;
        val permissionIntent = PendingIntent.getBroadcast(this, 0, Intent(ACTION_USB_PERMISSION), 0)
        val filter = IntentFilter(ACTION_USB_PERMISSION)
        registerReceiver(usbReceiver, filter)

        // Get permission for all USB devices
        val devList = usbManager!!.getDeviceList();
        for ((name, dev) in devList) {
            usbManager!!.requestPermission(dev, permissionIntent);
        }

        // Ask for internet permission
        checkAndAsk(Manifest.permission.INTERNET);

        super.onCreate(savedInstanceState)
        hideSystemBars();
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
