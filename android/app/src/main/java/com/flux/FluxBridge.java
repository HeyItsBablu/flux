package com.flux;

import android.app.Activity;
import android.content.ClipData;
import android.content.ContentResolver;
import android.content.Intent;
import android.net.Uri;

public class FluxBridge {

    static {
        System.loadLibrary("native-lib"); // must match your CMakeLists target
    }

    private static native void nativeOnFilePickerResult(
            int             requestCode,
            int             resultCode,
            String[]        uris,
            ContentResolver resolver);

    public static void onFilePickerResult(Activity activity,
                                          int      requestCode,
                                          int      resultCode,
                                          Intent   data) {
        ContentResolver resolver = activity.getContentResolver();
        if (resultCode != Activity.RESULT_OK || data == null) {
            nativeOnFilePickerResult(requestCode, resultCode, null, resolver);
            return;
        }
        String[] uris = collectUris(data);
        nativeOnFilePickerResult(requestCode, resultCode, uris, resolver);
    }

    private static String[] collectUris(Intent data) {
        ClipData clip = data.getClipData();
        if (clip != null && clip.getItemCount() > 0) {
            String[] uris = new String[clip.getItemCount()];
            for (int i = 0; i < clip.getItemCount(); i++) {
                Uri uri = clip.getItemAt(i).getUri();
                uris[i] = uri != null ? uri.toString() : "";
            }
            return uris;
        }
        Uri single = data.getData();
        if (single != null) return new String[]{ single.toString() };
        return new String[0];
    }

    public static void launchFilePicker(Activity activity, Intent intent, int requestCode) {
        activity.startActivityForResult(intent, requestCode);
    }
}