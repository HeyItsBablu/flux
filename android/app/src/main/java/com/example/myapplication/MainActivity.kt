package com.example.myapplication

import android.app.NativeActivity
import android.content.Intent
import android.os.Bundle
import com.flux.FluxBridge

class MainActivity : NativeActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
    }

    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        FluxBridge.onFilePickerResult(this, requestCode, resultCode, data)
    }
}