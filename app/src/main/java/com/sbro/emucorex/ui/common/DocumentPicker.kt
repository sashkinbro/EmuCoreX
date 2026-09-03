package com.sbro.emucorex.ui.common

import android.content.ActivityNotFoundException
import android.content.Context
import android.widget.Toast
import androidx.activity.result.ActivityResultLauncher
import com.sbro.emucorex.R

/**
 * Launches a document picker, showing a message instead of crashing when the
 * device has no app that handles the picker intent (e.g. stripped-down ROMs
 * without DocumentsUI). This is a top crash in Android Vitals
 * (ActivityNotFoundException for ACTION_OPEN_DOCUMENT_TREE on onboarding).
 */
fun <I> ActivityResultLauncher<I>.safeLaunch(
    input: I,
    onMissingHandler: () -> Unit
) {
    try {
        launch(input)
    } catch (_: ActivityNotFoundException) {
        onMissingHandler()
    }
}

fun Context.showNoDocumentPickerMessage() {
    Toast.makeText(this, getString(R.string.picker_no_document_handler), Toast.LENGTH_LONG).show()
}
