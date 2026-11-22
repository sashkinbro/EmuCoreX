package com.sbro.emucorex.ps2ui

import java.io.File
import java.io.RandomAccessFile
import java.nio.charset.Charset
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import kotlin.math.abs
import kotlin.math.max
import kotlin.math.min

data class Ps2McSaveEntry(
    val dirName: String,
    val title: String,
    val sizeKb: Int,
    val dateStr: String,
    val colorRgb: Int,
    val iconVerts: FloatArray?,
)

data class Ps2McContents(
    val cardName: String,
    val cardPath: String,
    val freeKb: Int,
    val totalKb: Int,
    val saves: List<Ps2McSaveEntry>,
)

/**
 * Pure read-only PlayStation 2 Memory Card (`.ps2` MCFS image & folder card)
 * and `.icn` 3D save-icon mesh decoder. Never opens any card for writing.
 */
object Ps2MemoryCardParser {
    private val MAGIC = "Sony PS2 Memory Card Format ".toByteArray(Charsets.US_ASCII)
    private val SHIFT_JIS: Charset = runCatching { Charset.forName("Shift_JIS") }
        .getOrElse { Charsets.UTF_8 }

    private const val DF_FILE = 0x0010
    private const val DF_DIR = 0x0020
    private const val DF_EXISTS = 0x8000
    private const val MAX_ICON_BYTES = 512 * 1024
    private const val MAX_ICON_VERTS = 4500

    fun parse(cardPath: String, cardName: String): Ps2McContents {
        val file = File(cardPath)
        if (!file.exists()) {
            return Ps2McContents(cardName, cardPath, 0, 0, emptyList())
        }
        if (file.isDirectory) {
            return parseFolderCard(file, cardName)
        }
        return runCatching { parseMcfsImage(file, cardName) }
            .getOrElse { Ps2McContents(cardName, cardPath, 0, 0, emptyList()) }
    }

    private fun parseFolderCard(rootDir: File, cardName: String): Ps2McContents {
        val subDirs = rootDir.listFiles()?.filter { it.isDirectory } ?: emptyList()
        val entries = ArrayList<Ps2McSaveEntry>(subDirs.size)
        val dateFmt = SimpleDateFormat("dd/MM/yyyy HH:mm", Locale.getDefault())
        var usedKbTotal = 0

        for (dir in subDirs) {
            val files = dir.walkTopDown().filter { it.isFile }.toList()
            val totalBytes = files.sumOf { it.length().coerceAtLeast(0L) }
            val sizeKb = ((totalBytes + 1023L) / 1024L).toInt().coerceAtLeast(1)
            usedKbTotal += sizeKb

            val iconSysFile = files.firstOrNull { it.name.equals("icon.sys", ignoreCase = true) }
            val iconSysBytes = iconSysFile?.takeIf { it.length() in 192..4096 }?.let {
                runCatching { it.readBytes() }.getOrNull()
            }
            val parsedSys = parseIconSys(iconSysBytes)
            val title = parsedSys?.title?.ifBlank { dir.name } ?: dir.name

            val targetIconName = parsedSys?.iconFileName
            val icnFile = if (!targetIconName.isNullOrBlank()) {
                files.firstOrNull { it.name.equals(targetIconName, ignoreCase = true) }
            } else {
                null
            } ?: files.firstOrNull { it.name.endsWith(".icn", ignoreCase = true) }
                ?: files.firstOrNull { it.name.endsWith(".ico", ignoreCase = true) }

            val icnBytes = icnFile?.takeIf { it.length() in 32..MAX_ICON_BYTES }?.let {
                runCatching { it.readBytes() }.getOrNull()
            }
            val colorRgb = parsedSys?.colorRgb ?: fallbackColorForName(dir.name)
            val mesh = decodeIcnMesh(icnBytes, colorRgb)
            val newestMs = files.maxOfOrNull { it.lastModified() } ?: dir.lastModified()
            val dateStr = if (newestMs > 0L) dateFmt.format(Date(newestMs)) else ""

            entries.add(
                Ps2McSaveEntry(
                    dirName = dir.name,
                    title = title,
                    sizeKb = sizeKb,
                    dateStr = dateStr,
                    colorRgb = colorRgb,
                    iconVerts = mesh,
                )
            )
        }

        entries.sortBy { it.title.lowercase(Locale.ROOT) }
        val virtualTotalKb = max(8192, usedKbTotal + 8000)
        val freeKb = (virtualTotalKb - usedKbTotal).coerceAtLeast(0)
        return Ps2McContents(cardName, rootDir.absolutePath, freeKb, virtualTotalKb, entries)
    }

    private class McfsReader(
        private val raf: RandomAccessFile,
        private val fileLen: Long,
        val pageLen: Int,
        val rawPageStride: Int,
        val pagesPerCluster: Int,
        val clustersPerCard: Int,
        val allocOffset: Int,
        val allocEnd: Int,
        val rootDirCluster: Int,
        val fat: IntArray,
    ) {
        val clusterSize: Int = pageLen * pagesPerCluster

        fun readPhysicalCluster(physCluster: Int): ByteArray? {
            if (physCluster < 0 || physCluster >= clustersPerCard) return null
            val out = ByteArray(clusterSize)
            for (p in 0 until pagesPerCluster) {
                val pageIdx = physCluster.toLong() * pagesPerCluster + p
                val offset = pageIdx * rawPageStride
                if (offset + pageLen > fileLen) return null
                raf.seek(offset)
                raf.readFully(out, p * pageLen, pageLen)
            }
            return out
        }

        fun readChain(startRelCluster: Int, maxBytes: Int): ByteArray? {
            if (startRelCluster < 0 || startRelCluster >= allocEnd) return null
            val chunks = ArrayList<ByteArray>(8)
            var cur = startRelCluster
            var total = 0
            var steps = 0
            val maxSteps = min(allocEnd + 2, (maxBytes + clusterSize - 1) / clusterSize + 2)
            while (cur in 0 until allocEnd && steps < maxSteps) {
                val cData = readPhysicalCluster(allocOffset + cur) ?: break
                chunks.add(cData)
                total += cData.size
                if (total >= maxBytes) break
                val nextRaw = if (cur < fat.size) fat[cur] else -1
                if (nextRaw == -1 || (nextRaw and 0x80000000.toInt()) == 0) break
                val next = nextRaw and 0x7FFFFFFF
                if (next == 0x7FFFFFFF || next == cur) break
                cur = next
                steps++
            }
            val resultSize = min(total, maxBytes)
            val out = ByteArray(resultSize)
            var pos = 0
            for (chunk in chunks) {
                val copyLen = min(chunk.size, resultSize - pos)
                if (copyLen <= 0) break
                System.arraycopy(chunk, 0, out, pos, copyLen)
                pos += copyLen
            }
            return out
        }
    }

    private data class McfsDirItem(
        val mode: Int,
        val length: Int,
        val startCluster: Int,
        val dateStr: String,
        val name: String,
    )

    private fun parseDirEntries(bytes: ByteArray, limitCount: Int): List<McfsDirItem> {
        val count = min(bytes.size / 512, max(0, limitCount))
        val out = ArrayList<McfsDirItem>(count)
        for (i in 0 until count) {
            val base = i * 512
            val mode = u16(bytes, base)
            if ((mode and DF_EXISTS) == 0) continue
            val length = u32(bytes, base + 0x04)
            val startCluster = u32(bytes, base + 0x10)
            val year = u16(bytes, base + 0x1E)
            val month = (bytes[base + 0x1D].toInt() and 0xFF).coerceIn(1, 12)
            val day = (bytes[base + 0x1C].toInt() and 0xFF).coerceIn(1, 31)
            val hour = (bytes[base + 0x1B].toInt() and 0xFF).coerceIn(0, 23)
            val minute = (bytes[base + 0x1A].toInt() and 0xFF).coerceIn(0, 59)
            val name = readCString(bytes, base + 0x40, 32)
            if (name.isEmpty() || name == "." || name == "..") continue
            val dateStr = if (year in 1995..2100) {
                String.format(Locale.US, "%02d/%02d/%04d %02d:%02d", day, month, year, hour, minute)
            } else {
                ""
            }
            out.add(McfsDirItem(mode, length, startCluster, dateStr, name))
        }
        return out
    }

    private fun parseMcfsImage(file: File, cardName: String): Ps2McContents {
        RandomAccessFile(file, "r").use { raf ->
            val fileLen = raf.length()
            if (fileLen < 8192L) {
                return Ps2McContents(cardName, file.absolutePath, 0, 0, emptyList())
            }
            val superBuf = ByteArray(512)
            raf.seek(0L)
            raf.readFully(superBuf)
            for (i in MAGIC.indices) {
                if (superBuf[i] != MAGIC[i]) {
                    return Ps2McContents(cardName, file.absolutePath, 0, 0, emptyList())
                }
            }

            val pageLen = u16(superBuf, 0x28).takeIf { it in 256..4096 } ?: 512
            val pagesPerCluster = u16(superBuf, 0x2A).takeIf { it in 1..16 } ?: 2
            val clustersPerCard = u32(superBuf, 0x30)
            val allocOffset = u32(superBuf, 0x34)
            val allocEnd = u32(superBuf, 0x38).coerceAtMost(262144)
            val rootCluster = u32(superBuf, 0x3C)
            if (clustersPerCard <= 0 || allocEnd <= 0 || allocOffset + allocEnd > clustersPerCard) {
                return Ps2McContents(cardName, file.absolutePath, 0, 0, emptyList())
            }

            val withEccBytes = clustersPerCard.toLong() * pagesPerCluster * (pageLen + 16)
            val rawPageStride = if (fileLen >= withEccBytes) pageLen + 16 else pageLen
            val clusterSize = pageLen * pagesPerCluster

            fun readPhysClusterRaw(phys: Int): ByteArray? {
                if (phys < 0 || phys >= clustersPerCard) return null
                val out = ByteArray(clusterSize)
                for (p in 0 until pagesPerCluster) {
                    val off = (phys.toLong() * pagesPerCluster + p) * rawPageStride
                    if (off + pageLen > fileLen) return null
                    raf.seek(off)
                    raf.readFully(out, p * pageLen, pageLen)
                }
                return out
            }

            val entriesPerCluster = clusterSize / 4
            val fat = IntArray(allocEnd) { -1 }
            var fatIdx = 0
            for (ifcSlot in 0 until 32) {
                if (fatIdx >= allocEnd) break
                val ifcCluster = u32(superBuf, 0x50 + ifcSlot * 4)
                if (ifcCluster <= 0 || ifcCluster >= clustersPerCard) continue
                val ifcData = readPhysClusterRaw(ifcCluster) ?: continue
                for (j in 0 until entriesPerCluster) {
                    if (fatIdx >= allocEnd) break
                    val fatCluster = u32(ifcData, j * 4)
                    if (fatCluster <= 0 || fatCluster >= clustersPerCard) {
                        fatIdx += entriesPerCluster
                        continue
                    }
                    val fatData = readPhysClusterRaw(fatCluster) ?: continue
                    for (k in 0 until entriesPerCluster) {
                        if (fatIdx >= allocEnd) break
                        fat[fatIdx++] = u32(fatData, k * 4)
                    }
                }
            }

            var freeClusters = 0
            for (c in 0 until allocEnd) {
                if ((fat[c] and 0x80000000.toInt()) == 0) {
                    freeClusters++
                }
            }
            val totalKb = ((allocEnd.toLong() * clusterSize) / 1024L).toInt()
            val freeKb = ((freeClusters.toLong() * clusterSize) / 1024L).toInt()

            val reader = McfsReader(
                raf = raf,
                fileLen = fileLen,
                pageLen = pageLen,
                rawPageStride = rawPageStride,
                pagesPerCluster = pagesPerCluster,
                clustersPerCard = clustersPerCard,
                allocOffset = allocOffset,
                allocEnd = allocEnd,
                rootDirCluster = rootCluster,
                fat = fat,
            )

            // Root "." entry tells us how many entries exist in root
            val rootHead = reader.readPhysicalCluster(allocOffset + rootCluster) ?: return Ps2McContents(
                cardName, file.absolutePath, freeKb, totalKb, emptyList()
            )
            val rootEntryCount = u32(rootHead, 0x04).coerceIn(2, 512)
            val rootDirBytes = reader.readChain(rootCluster, rootEntryCount * 512) ?: return Ps2McContents(
                cardName, file.absolutePath, freeKb, totalKb, emptyList()
            )
            val rootDirs = parseDirEntries(rootDirBytes, rootEntryCount)
            val saves = ArrayList<Ps2McSaveEntry>(rootDirs.size)

            for (dirItem in rootDirs) {
                if ((dirItem.mode and DF_DIR) == 0) continue
                val childCount = dirItem.length.coerceIn(2, 256)
                val subBytes = reader.readChain(dirItem.startCluster, childCount * 512) ?: continue
                val subItems = parseDirEntries(subBytes, childCount)

                var totalSubBytes = 0L
                var iconSysItem: McfsDirItem? = null
                val icnCandidates = ArrayList<McfsDirItem>()
                for (sub in subItems) {
                    if ((sub.mode and DF_FILE) == 0) continue
                    totalSubBytes += sub.length.toLong().coerceAtLeast(0L)
                    if (sub.name.equals("icon.sys", ignoreCase = true)) {
                        iconSysItem = sub
                    } else if (sub.name.endsWith(".icn", ignoreCase = true) ||
                        sub.name.endsWith(".ico", ignoreCase = true)
                    ) {
                        icnCandidates.add(sub)
                    }
                }

                val sysBytes = iconSysItem?.takeIf { it.length in 192..4096 }?.let {
                    reader.readChain(it.startCluster, it.length)
                }
                val parsedSys = parseIconSys(sysBytes)
                val title = parsedSys?.title?.ifBlank { dirItem.name } ?: dirItem.name

                val targetIcn = parsedSys?.iconFileName?.let { want ->
                    icnCandidates.firstOrNull { it.name.equals(want, ignoreCase = true) }
                } ?: icnCandidates.firstOrNull()

                val icnBytes = targetIcn?.takeIf { it.length in 32..MAX_ICON_BYTES }?.let {
                    reader.readChain(it.startCluster, it.length)
                }
                val colorRgb = parsedSys?.colorRgb ?: fallbackColorForName(dirItem.name)
                val mesh = decodeIcnMesh(icnBytes, colorRgb)
                val sizeKb = ((totalSubBytes + 1023L) / 1024L).toInt().coerceAtLeast(1)

                saves.add(
                    Ps2McSaveEntry(
                        dirName = dirItem.name,
                        title = title,
                        sizeKb = sizeKb,
                        dateStr = dirItem.dateStr,
                        colorRgb = colorRgb,
                        iconVerts = mesh,
                    )
                )
            }

            saves.sortBy { it.title.lowercase(Locale.ROOT) }
            return Ps2McContents(cardName, file.absolutePath, freeKb, totalKb, saves)
        }
    }

    private data class ParsedIconSys(
        val title: String,
        val iconFileName: String,
        val colorRgb: Int,
    )

    private fun parseIconSys(bytes: ByteArray?): ParsedIconSys? {
        if (bytes == null || bytes.size < 0x144) return null
        if (bytes[0] != 'P'.code.toByte() || bytes[1] != 'S'.code.toByte() ||
            bytes[2] != '2'.code.toByte() || bytes[3] != 'D'.code.toByte()
        ) {
            return null
        }
        val line2Off = u16(bytes, 0x06)

        // Average top-left & bottom-right background corners in icon.sys for authentic PS2 card tint
        var rSum = 0
        var gSum = 0
        var bSum = 0
        for (c in 0 until 4) {
            val o = 0x10 + c * 16
            rSum += u32(bytes, o).coerceIn(0, 255)
            gSum += u32(bytes, o + 4).coerceIn(0, 255)
            bSum += u32(bytes, o + 8).coerceIn(0, 255)
        }
        var avgR = (rSum / 4).coerceIn(0, 255)
        var avgG = (gSum / 4).coerceIn(0, 255)
        var avgB = (bSum / 4).coerceIn(0, 255)
        if (avgR + avgG + avgB < 90) {
            avgR = 88
            avgG = 165
            avgB = 255
        }
        val colorRgb = (avgR shl 16) or (avgG shl 8) or avgB

        var titleEnd = 0xC0
        while (titleEnd < 0x104 && bytes[titleEnd] != 0.toByte()) {
            titleEnd++
        }
        val rawTitleBytes = bytes.copyOfRange(0xC0, titleEnd)
        val rawTitle = if (line2Off in 1 until rawTitleBytes.size) {
            val l1 = decodeShiftJisClean(rawTitleBytes.copyOfRange(0, line2Off))
            val l2 = decodeShiftJisClean(rawTitleBytes.copyOfRange(line2Off, rawTitleBytes.size))
            if (l2.isBlank()) l1 else "$l1 $l2".trim()
        } else {
            decodeShiftJisClean(rawTitleBytes)
        }

        val iconFile = readCString(bytes, 0x104, 64)
        return ParsedIconSys(rawTitle, iconFile, colorRgb)
    }

    private fun decodeShiftJisClean(raw: ByteArray): String {
        val decoded = String(raw, SHIFT_JIS)
        val sb = StringBuilder(decoded.length)
        for (ch in decoded) {
            when {
                ch == '\u0000' -> break
                ch == '\u3000' -> sb.append(' ')
                ch in '\uFF01'..'\uFF5E' -> sb.append((ch.code - 0xFEE0).toChar())
                ch == '\uFF5E' || ch == '\u301C' -> sb.append('~')
                ch.code >= 0x20 -> sb.append(ch)
            }
        }
        return sb.toString().replace(Regex("\\s+"), " ").trim()
    }

    /**
     * Decodes shape 0 of a PS2 `.icn` 3D mesh into normalized vertices:
     * `[x, y, z, nx, ny, nz, r, g, b]` * vertexCount.
     */
    private fun decodeIcnMesh(bytes: ByteArray?, tintRgb: Int): FloatArray? {
        if (bytes == null || bytes.size < 64) return null
        val magic = u32(bytes, 0x00)
        if (magic != 0x00010000) return null
        val animShapes = u32(bytes, 0x04)
        val flags = u32(bytes, 0x08)
        val vertexCount = u32(bytes, 0x10)
        if (animShapes !in 1..64) return null
        if (vertexCount !in 3..MAX_ICON_VERTS || vertexCount % 3 != 0) return null

        val vertStride = 8 * animShapes + 16
        val tableEnd = 0x14 + vertexCount.toLong() * vertStride
        if (tableEnd > bytes.size.toLong()) return null

        // Optionally decode uncompressed 128x128 A1B5G5R5 texture at the end for rich colored icons
        val texPixels = tryExtractIcnTexture(bytes, tableEnd.toInt(), flags)

        val positions = FloatArray(vertexCount * 3)
        val normals = FloatArray(vertexCount * 3)
        val colors = FloatArray(vertexCount * 3)

        var minX = Float.POSITIVE_INFINITY
        var maxX = Float.NEGATIVE_INFINITY
        var minY = Float.POSITIVE_INFINITY
        var maxY = Float.NEGATIVE_INFINITY
        var minZ = Float.POSITIVE_INFINITY
        var maxZ = Float.NEGATIVE_INFINITY

        val tr = ((tintRgb ushr 16) and 0xFF) / 255f
        val tg = ((tintRgb ushr 8) and 0xFF) / 255f
        val tb = (tintRgb and 0xFF) / 255f

        var off = 0x14
        for (v in 0 until vertexCount) {
            val px = s16(bytes, off) / 4096f
            val py = -s16(bytes, off + 2) / 4096f
            val pz = s16(bytes, off + 4) / 4096f
            positions[v * 3] = px
            positions[v * 3 + 1] = py
            positions[v * 3 + 2] = pz
            minX = min(minX, px); maxX = max(maxX, px)
            minY = min(minY, py); maxY = max(maxY, py)
            minZ = min(minZ, pz); maxZ = max(maxZ, pz)

            val attrOff = off + animShapes * 8
            var nx = s16(bytes, attrOff) / 4096f
            var ny = -s16(bytes, attrOff + 2) / 4096f
            var nz = s16(bytes, attrOff + 4) / 4096f
            val nLen = kotlin.math.sqrt(nx * nx + ny * ny + nz * nz)
            if (nLen > 0.001f) {
                nx /= nLen; ny /= nLen; nz /= nLen
            } else {
                nx = 0f; ny = 1f; nz = 0f
            }
            normals[v * 3] = nx
            normals[v * 3 + 1] = ny
            normals[v * 3 + 2] = nz

            val u = (s16(bytes, attrOff + 8) / 4096f).coerceIn(0f, 0.999f)
            val vv = (s16(bytes, attrOff + 10) / 4096f).coerceIn(0f, 0.999f)
            var vr = (bytes[attrOff + 12].toInt() and 0xFF) / 128f
            var vg = (bytes[attrOff + 13].toInt() and 0xFF) / 128f
            var vb = (bytes[attrOff + 14].toInt() and 0xFF) / 128f

            if (texPixels != null) {
                val tx = (u * 127.9f).toInt().coerceIn(0, 127)
                val ty = (vv * 127.9f).toInt().coerceIn(0, 127)
                val argb = texPixels[ty * 128 + tx]
                val texR = ((argb ushr 16) and 0xFF) / 255f
                val texG = ((argb ushr 8) and 0xFF) / 255f
                val texB = (argb and 0xFF) / 255f
                vr = (vr * texR).coerceIn(0f, 1f)
                vg = (vg * texG).coerceIn(0f, 1f)
                vb = (vb * texB).coerceIn(0f, 1f)
            } else {
                vr = vr.coerceIn(0f, 1f)
                vg = vg.coerceIn(0f, 1f)
                vb = vb.coerceIn(0f, 1f)
                if (vr + vg + vb < 0.22f) {
                    vr = 0.35f + 0.55f * tr
                    vg = 0.35f + 0.55f * tg
                    vb = 0.35f + 0.55f * tb
                }
            }

            colors[v * 3] = vr
            colors[v * 3 + 1] = vg
            colors[v * 3 + 2] = vb

            off += vertStride
        }

        val cx = (minX + maxX) * 0.5f
        val cy = (minY + maxY) * 0.5f
        val cz = (minZ + maxZ) * 0.5f
        val span = max(0.001f, max(maxX - minX, max(maxY - minY, maxZ - minZ)))
        val normScale = 1.0f / span

        val packed = FloatArray(vertexCount * 9)
        for (v in 0 until vertexCount) {
            val base = v * 9
            packed[base + 0] = (positions[v * 3] - cx) * normScale
            packed[base + 1] = (positions[v * 3 + 1] - cy) * normScale
            packed[base + 2] = (positions[v * 3 + 2] - cz) * normScale
            packed[base + 3] = normals[v * 3]
            packed[base + 4] = normals[v * 3 + 1]
            packed[base + 5] = normals[v * 3 + 2]
            packed[base + 6] = colors[v * 3]
            packed[base + 7] = colors[v * 3 + 1]
            packed[base + 8] = colors[v * 3 + 2]
        }
        return packed
    }

    private fun tryExtractIcnTexture(bytes: ByteArray, animHeaderOff: Int, flags: Int): IntArray? {
        if ((flags and 0x04) == 0) return null
        if (animHeaderOff + 20 > bytes.size) return null
        val animHeaderId = u32(bytes, animHeaderOff)
        if (animHeaderId != 0x01) {
            // Uncompressed fallback: PS2 uncompressed texture is exact 128*128*2 = 32768 bytes at file tail
            if ((flags and 0x08) == 0 && bytes.size >= animHeaderOff + 32768 + 20) {
                return decodeRgb1555(bytes, bytes.size - 32768)
            }
            return null
        }
        val numFrames = u32(bytes, animHeaderOff + 16).coerceIn(0, 512)
        var off = animHeaderOff + 20
        for (f in 0 until numFrames) {
            if (off + 16 > bytes.size) return null
            val keyCount = u32(bytes, off + 12).coerceIn(0, 512)
            off += 16 + keyCount * 8
        }
        if ((flags and 0x08) != 0) {
            // RLE compressed texture
            if (off + 4 > bytes.size) return null
            val compSize = u32(bytes, off)
            off += 4
            if (compSize <= 0 || off + compSize > bytes.size) return null
            val out = IntArray(128 * 128)
            var outIdx = 0
            val endOff = off + compSize
            var p = off
            while (p + 2 <= endOff && outIdx < out.size) {
                val code = u16(bytes, p)
                p += 2
                if (code < 0xFF00) {
                    if (p + 2 > endOff) break
                    val rep = code.coerceAtLeast(1)
                    val px = rgb1555ToArgb(u16(bytes, p))
                    p += 2
                    for (r in 0 until rep) {
                        if (outIdx >= out.size) break
                        out[outIdx++] = px
                    }
                } else {
                    val lit = 0x10000 - code
                    for (l in 0 until lit) {
                        if (p + 2 > endOff || outIdx >= out.size) break
                        out[outIdx++] = rgb1555ToArgb(u16(bytes, p))
                        p += 2
                    }
                }
            }
            return if (outIdx >= 128) out else null
        } else {
            if (off + 32768 > bytes.size) return null
            return decodeRgb1555(bytes, off)
        }
    }

    private fun decodeRgb1555(bytes: ByteArray, start: Int): IntArray {
        val out = IntArray(128 * 128)
        for (i in out.indices) {
            out[i] = rgb1555ToArgb(u16(bytes, start + i * 2))
        }
        return out
    }

    private fun rgb1555ToArgb(c: Int): Int {
        val r = ((c and 0x1F) * 255) / 31
        val g = (((c ushr 5) and 0x1F) * 255) / 31
        val b = (((c ushr 10) and 0x1F) * 255) / 31
        return (0xFF shl 24) or (r shl 16) or (g shl 8) or b
    }

    private fun fallbackColorForName(name: String): Int {
        val h = abs(name.hashCode())
        val palette = intArrayOf(0x46A6FF, 0x52E2B8, 0xFF6B9E, 0xA882FF, 0xFFC857, 0x56D8FF)
        return palette[h % palette.size]
    }

    private fun u16(b: ByteArray, o: Int): Int =
        (b[o].toInt() and 0xFF) or ((b[o + 1].toInt() and 0xFF) shl 8)

    private fun s16(b: ByteArray, o: Int): Int {
        val v = u16(b, o)
        return if (v >= 0x8000) v - 0x10000 else v
    }

    private fun u32(b: ByteArray, o: Int): Int =
        (b[o].toInt() and 0xFF) or
            ((b[o + 1].toInt() and 0xFF) shl 8) or
            ((b[o + 2].toInt() and 0xFF) shl 16) or
            ((b[o + 3].toInt() and 0xFF) shl 24)

    private fun readCString(b: ByteArray, start: Int, maxLen: Int): String {
        val endLimit = min(b.size, start + maxLen)
        var end = start
        while (end < endLimit && b[end] != 0.toByte()) {
            end++
        }
        return if (end > start) String(b, start, end - start, Charsets.US_ASCII).trim() else ""
    }
}
