// FluxBridge.java
// Place at: app/src/main/java/com/flux/FluxBridge.java
//
// This class:
//   1. Receives onActivityResult() from your Activity
//   2. Unpacks single or multiple URIs from the Intent
//   3. Calls nativeOnFilePickerResult() to deliver paths back to C++
//
// Setup in your Activity (or GameActivity wrapper):
//
//   import com.flux.FluxBridge;
//
//   @Override
//   protected void onActivityResult(int requestCode, int resultCode, Intent data) {
//       super.onActivityResult(requestCode, resultCode, data);
//       FluxBridge.onFilePickerResult(this, requestCode, resultCode, data);
//   }
//
// ============================================================================

package com.flux;

import android.app.Activity;
import android.content.ClipData;
import android.content.ContentResolver;
import android.content.Intent;
import android.net.Uri;

public class FluxBridge {

    // ── Load native library ──────────────────────────────────────────────────
    // The native lib name should match what your CMakeLists.txt produces.
    // Typically this is called once from Application.onCreate() or Activity.onCreate().
    static {
        System.loadLibrary("flux_app"); // change to your actual library name
    }

    // ── Native callback (implemented in flux_file_picker_android.hpp) ────────
    private static native void nativeOnFilePickerResult(
            int      requestCode,
            int      resultCode,
            String[] uris,
            ContentResolver resolver);

    // ── Called from Activity.onActivityResult() ──────────────────────────────
    public static void onFilePickerResult(Activity    activity,
                                          int         requestCode,
                                          int         resultCode,
                                          Intent      data) {

        ContentResolver resolver = activity.getContentResolver();

        // Cancelled or no data
        if (resultCode != Activity.RESULT_OK || data == null) {
            nativeOnFilePickerResult(requestCode, resultCode, null, resolver);
            return;
        }

        String[] uriStrings = collectUris(data);
        nativeOnFilePickerResult(requestCode, resultCode, uriStrings, resolver);
    }

    // ── Collect all URIs from an Intent ──────────────────────────────────────
    // Handles both single-select (getData()) and multi-select (getClipData()).
    private static String[] collectUris(Intent data) {
        // Multi-select: ClipData holds all selected URIs
        ClipData clip = data.getClipData();
        if (clip != null && clip.getItemCount() > 0) {
            String[] uris = new String[clip.getItemCount()];
            for (int i = 0; i < clip.getItemCount(); i++) {
                Uri uri = clip.getItemAt(i).getUri();
                uris[i] = uri != null ? uri.toString() : "";
            }
            return uris;
        }

        // Single-select: just getData()
        Uri single = data.getData();
        if (single != null) {
            return new String[]{ single.toString() };
        }

        return new String[0];
    }
}