package com.sbro.emucorex.core

/**
 * Pure document-ID helpers used by arcade SAF asset resolution.
 *
 * Providers expose document IDs in different shapes: external storage and AnExplorer use
 * hierarchical `volume:path/to/file` IDs, while some file managers return opaque IDs.
 * These helpers only cover the hierarchical shape; callers fall back to child enumeration
 * when a document ID does not follow it.
 */
internal object ArcadeDocumentIds {

    /** Parent directory document ID, or null when [documentId] is opaque. */
    fun parentDocumentId(documentId: String): String? {
        val slash = documentId.lastIndexOf('/')
        if (slash > 0) return documentId.substring(0, slash)
        val colon = documentId.lastIndexOf(':')
        if (colon >= 0) return documentId.substring(0, colon + 1)
        return null
    }

    /** Appends a child name to a hierarchical parent document ID. */
    fun appendDocumentId(parentId: String, name: String): String =
        if (parentId.endsWith(':') || parentId.endsWith('/')) "$parentId$name" else "$parentId/$name"

    /**
     * One directory up, or null when already at the volume root / opaque ID.
     *
     * [floorId] clamps `..` at the granted tree root. Native asset candidates use
     * `../../memcards/<file>` for shared dongle folders that actually sit one level above
     * the game directory, so walking above the user-granted directory must not escape it.
     */
    fun ascendDocumentId(documentId: String, floorId: String? = null): String? {
        if (floorId != null && documentId == floorId) return floorId
        val slash = documentId.lastIndexOf('/')
        if (slash > 0) return documentId.substring(0, slash)
        val colon = documentId.lastIndexOf(':')
        if (colon < 0) return null
        val root = documentId.substring(0, colon + 1)
        return if (documentId == root) null else root
    }
}
