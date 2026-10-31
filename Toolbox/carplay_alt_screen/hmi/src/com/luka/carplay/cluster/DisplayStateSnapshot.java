package com.luka.carplay.cluster;

/** Validates the map observer's QNX shared-memory in-place publication. */
public final class DisplayStateSnapshot {
    private DisplayStateSnapshot() { }
    public static String decode(byte[] bytes, int length) {
        if (bytes == null || length <= 0 || length > bytes.length) return "";
        try {
            String text = new String(bytes, 0, length, "UTF-8");
            // Older/atomic snapshots always start with schema=1. A partial new
            // envelope must never be mistaken for a legacy unframed snapshot.
            if (!text.startsWith("snapshot_format=2\n"))
                return text.startsWith("schema=1\n") && text.endsWith("\n") ? text : "";
            int first = text.indexOf('\n');
            int second = text.indexOf('\n', first + 1);
            int third = text.indexOf('\n', second + 1);
            if (second < 0 || third < 0) return "";
            String sizeLine = text.substring(first + 1, second);
            String hashLine = text.substring(second + 1, third);
            if (!sizeLine.startsWith("snapshot_bytes=") || !hashLine.startsWith("snapshot_hash=")) return "";
            int size = Integer.parseInt(sizeLine.substring(15));
            String hashText = hashLine.substring(14);
            if (size <= 0 || size > 3072 || hashText.length() != 8) return "";
            long expected = Long.parseLong(hashText, 16);
            // Envelope header is ASCII; offsets here are byte offsets, not
            // UTF-8 character offsets in the potentially Chinese body.
            int start = third + 1;
            String trailer = "snapshot_end=" + hashText + "\n";
            byte[] end = trailer.getBytes("UTF-8");
            if (start + size + end.length != length) return "";
            long hash = 2166136261L;
            for (int i = start; i < start + size; i++)
                hash = ((hash ^ (bytes[i] & 255)) * 16777619L) & 0xffffffffL;
            if (hash != expected) return "";
            for (int i = 0; i < end.length; i++)
                if (bytes[start + size + i] != end[i]) return "";
            String body = new String(bytes, start, size, "UTF-8");
            return body.startsWith("schema=1\n") && body.endsWith("\n") ? body : "";
        } catch (Exception e) { return ""; }
    }
}
