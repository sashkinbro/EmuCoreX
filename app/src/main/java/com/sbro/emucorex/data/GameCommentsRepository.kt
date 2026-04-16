package com.sbro.emucorex.data

import com.google.firebase.firestore.FirebaseFirestore
import com.google.firebase.firestore.ListenerRegistration
import com.google.firebase.firestore.Query
import java.util.Date

data class GameComment(
    val id: String,
    val displayName: String,
    val photoURL: String = "",
    val text: String,
    val rating: Int,
    val phoneBrand: String = "",
    val phoneModel: String = "",
    val phoneName: String = "",
    val phoneCpu: String = "",
    val phoneRam: String = "",
    val createdAt: Date? = null
) {
    val deviceTitle: String
        get() = phoneName.ifBlank {
            listOf(phoneBrand, phoneModel)
                .filter { it.isNotBlank() }
                .joinToString(" ")
        }

    val deviceSpecs: String
        get() = listOf(phoneCpu, phoneRam)
            .filter { it.isNotBlank() }
            .joinToString(" • ")
}

class GameCommentsRepository(
    private val firestore: FirebaseFirestore = FirebaseFirestore.getInstance()
) {

    fun observeComments(
        gameId: Long,
        onUpdate: (List<GameComment>) -> Unit,
        onError: (Throwable) -> Unit
    ): ListenerRegistration {
        return firestore.collection("games")
            .document(gameId.toString())
            .collection("comments")
            .orderBy("createdAt", Query.Direction.DESCENDING)
            .limit(60)
            .addSnapshotListener { snapshot, error ->
                if (error != null) {
                    onError(error)
                    return@addSnapshotListener
                }

                val comments = snapshot?.documents?.mapNotNull { document ->
                    val text = document.getString("text")?.trim().orEmpty()
                    if (text.isBlank()) return@mapNotNull null
                    GameComment(
                        id = document.id,
                        displayName = document.getString("displayName")?.trim().orEmpty().ifBlank { "Player" },
                        photoURL = document.getString("photoURL").orEmpty(),
                        text = text,
                        rating = (document.getLong("rating") ?: 0L).toInt().coerceIn(0, 5),
                        phoneBrand = document.getString("phoneBrand").orEmpty(),
                        phoneModel = document.getString("phoneModel").orEmpty(),
                        phoneName = document.getString("phoneName").orEmpty(),
                        phoneCpu = document.getString("phoneCpu").orEmpty(),
                        phoneRam = document.getString("phoneRam").orEmpty(),
                        createdAt = document.getTimestamp("createdAt")?.toDate()
                    )
                }.orEmpty()

                onUpdate(comments)
            }
    }
}
