// FluxBridge.java
// Place at: app/src/main/java/com/flux/FluxBridge.java
//
// Receives onActivityResult() from your Activity, unpacks the URI(s) from
// the Intent, and delivers them to C++ via nativeOnFilePickerResult().
//
// Setup — add to your Activity (or GameActivity wrapper):
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
    // Change "flux_app" to match the actual library name in your CMakeLists.txt.
    // Call System.loadLibrary() once — typically from Application.onCreate()
    // or Activity.onCreate() — before any FilePicker is used.
    static {
        System.loadLibrary("flux_app");
    }

    // ── JNI declaration — implemented in flux_file_picker_android.cpp ────────
    private static native void nativeOnFilePickerResult(
            int             requestCode,
            int             resultCode,
            String[]        uris,
            ContentResolver resolver);

    // ── Called from Activity.onActivityResult() ──────────────────────────────
    public static void onFilePickerResult(Activity activity,
                                          int      requestCode,
                                          int      resultCode,
                                          Intent   data) {

        ContentResolver resolver = activity.getContentResolver();

        // Cancelled or no data
        if (resultCode != Activity.RESULT_OK || data == null) {
            nativeOnFilePickerResult(requestCode, resultCode, null, resolver);
            return;
        }

        String[] uris = collectUris(data);
        nativeOnFilePickerResult(requestCode, resultCode, uris, resolver);
    }

    // ── Collect all URIs from an Intent ──────────────────────────────────────
    // Multi-select results are stored in ClipData; single-select uses getData().
    private static String[] collectUris(Intent data) {
        // Multi-select path
        ClipData clip = data.getClipData();
        if (clip != null && clip.getItemCount() > 0) {
            String[] uris = new String[clip.getItemCount()];
            for (int i = 0; i < clip.getItemCount(); i++) {
                Uri uri = clip.getItemAt(i).getUri();
                uris[i] = uri != null ? uri.toString() : "";
            }
            return uris;
        }

        // Single-select path
        Uri single = data.getData();
        if (single != null) {
            return new String[]{ single.toString() };
        }

        return new String[0];
    }
}