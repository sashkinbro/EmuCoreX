package com.sbro.emucorex.core;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.content.res.AssetFileDescriptor;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.net.Uri;
import android.os.CancellationSignal;
import android.os.Bundle;
import android.os.ParcelFileDescriptor;
import android.provider.DocumentsContract;

import java.io.File;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.IOException;
import java.util.List;

public final class TestGameDocumentsProvider extends ContentProvider {
    public static final String AUTHORITY = "com.sbro.emucorex.test.documents";
    public static final String ROOT_ID = "games-root";
    public static final String EMPTY_ROOT_ID = "empty-root";
    private static final String NESTED_ID = "games-root/nested";
    private static final String GAME_ID = "games-root/nested/game.iso";

    private static final String[] DOCUMENT_COLUMNS = {
        DocumentsContract.Document.COLUMN_DOCUMENT_ID,
        DocumentsContract.Document.COLUMN_DISPLAY_NAME,
        DocumentsContract.Document.COLUMN_MIME_TYPE,
        DocumentsContract.Document.COLUMN_SIZE,
        DocumentsContract.Document.COLUMN_FLAGS
    };

    @Override
    public boolean onCreate() {
        return true;
    }

    @Override
    public Cursor query(
        Uri uri,
        String[] projection,
        String selection,
        String[] selectionArgs,
        String sortOrder
    ) {
        final String[] columns = projection != null ? projection : DOCUMENT_COLUMNS;
        final MatrixCursor cursor = new MatrixCursor(columns);
        final List<String> segments = uri.getPathSegments();
        final int documentIndex = segments.indexOf("document");
        final String documentId = documentIndex >= 0 && documentIndex + 1 < segments.size()
            ? segments.get(documentIndex + 1)
            : ROOT_ID;
        if ("children".equals(uri.getLastPathSegment())) {
            if (ROOT_ID.equals(documentId)) {
                addDocumentRow(cursor, NESTED_ID);
            } else if (NESTED_ID.equals(documentId)) {
                addDocumentRow(cursor, GAME_ID);
            }
        } else {
            addDocumentRow(cursor, documentId);
        }
        return cursor;
    }

    @Override
    public ParcelFileDescriptor openFile(Uri uri, String mode, CancellationSignal signal)
        throws FileNotFoundException {
        final String documentId = DocumentsContract.getDocumentId(uri);
        if (!GAME_ID.equals(documentId)) {
            throw new FileNotFoundException(documentId);
        }
        final File file = new File(getContext().getCacheDir(), "provider-test-game.iso");
        if (!file.exists()) {
            try (FileOutputStream output = new FileOutputStream(file)) {
                output.write(new byte[] {1, 2, 3, 4});
            } catch (IOException error) {
                throw new FileNotFoundException(error.getMessage());
            }
        }
        return ParcelFileDescriptor.open(file, ParcelFileDescriptor.MODE_READ_ONLY);
    }

    @Override
    public AssetFileDescriptor openAssetFile(Uri uri, String mode) throws FileNotFoundException {
        final ParcelFileDescriptor descriptor = openFile(uri, mode, null);
        return new AssetFileDescriptor(descriptor, 0, AssetFileDescriptor.UNKNOWN_LENGTH);
    }

    @Override
    public AssetFileDescriptor openTypedAssetFile(
        Uri uri,
        String mimeTypeFilter,
        Bundle opts,
        CancellationSignal signal
    ) throws FileNotFoundException {
        final ParcelFileDescriptor descriptor = openFile(uri, "r", signal);
        return new AssetFileDescriptor(descriptor, 0, AssetFileDescriptor.UNKNOWN_LENGTH);
    }

    private static void addDocumentRow(MatrixCursor cursor, String documentId) {
        final String name;
        final String mimeType;
        final long size;
        if (ROOT_ID.equals(documentId)) {
            name = "Test games";
            mimeType = DocumentsContract.Document.MIME_TYPE_DIR;
            size = 0L;
        } else if (EMPTY_ROOT_ID.equals(documentId)) {
            name = "Empty games folder";
            mimeType = DocumentsContract.Document.MIME_TYPE_DIR;
            size = 0L;
        } else if (NESTED_ID.equals(documentId)) {
            name = "Provider folder without MIME";
            mimeType = null;
            size = 0L;
        } else if (GAME_ID.equals(documentId)) {
            name = "Gran Turismo 4.ISO";
            mimeType = null;
            size = 4L;
        } else {
            throw new IllegalArgumentException("Unknown document " + documentId);
        }
        cursor.newRow()
            .add(DocumentsContract.Document.COLUMN_DOCUMENT_ID, documentId)
            .add(DocumentsContract.Document.COLUMN_DISPLAY_NAME, name)
            .add(DocumentsContract.Document.COLUMN_MIME_TYPE, mimeType)
            .add(DocumentsContract.Document.COLUMN_SIZE, size)
            .add(DocumentsContract.Document.COLUMN_FLAGS, 0);
    }

    @Override
    public String getType(Uri uri) {
        return null;
    }

    @Override
    public Uri insert(Uri uri, ContentValues values) {
        return null;
    }

    @Override
    public int delete(Uri uri, String selection, String[] selectionArgs) {
        return 0;
    }

    @Override
    public int update(Uri uri, ContentValues values, String selection, String[] selectionArgs) {
        return 0;
    }
}
