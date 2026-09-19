package com.sbro.emucorex.ui.common

import androidx.compose.runtime.Composable
import androidx.compose.runtime.MutableState
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember

/**
 * Like [remember], but the value also survives the composable leaving and
 * re-entering the composition. Lazy list items are disposed when they scroll
 * out of view, which would otherwise reset their state, re-run their loaders
 * and replay their entrance animations on every scroll back.
 *
 * The value lives in a process-wide store under [key]; every caller must use a
 * unique, stable key and keep the value type consistent for that key.
 */
@Composable
fun <T> rememberRetainedState(key: String, initialValue: T): MutableState<T> =
    remember { RetainedStateStore.get(key, initialValue) }

private object RetainedStateStore {
    private val states = HashMap<String, MutableState<*>>()

    @Suppress("UNCHECKED_CAST")
    fun <T> get(key: String, initialValue: T): MutableState<T> =
        states.getOrPut(key) { mutableStateOf(initialValue) } as MutableState<T>
}
