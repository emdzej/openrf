import { basename } from "./path.js";
/** A `CsFile` over anything `Blob`-shaped. */
export class BlobFile {
    blob;
    mime;
    path;
    name;
    constructor(path, blob, 
    /** Overrides the blob's own type, which is often `""` from a file handle. */
    mime) {
        this.blob = blob;
        this.mime = mime;
        this.path = path;
        this.name = basename(path.split("#").at(-1) ?? path);
    }
    get size() {
        return this.blob.size;
    }
    get type() {
        return this.mime ?? this.blob.type ?? "";
    }
    slice(start, end) {
        return new BlobFile(this.path, this.blob.slice(start, end), this.mime);
    }
    /*
     * A `Blob` read cannot be cancelled, so a signal is honoured at the edges:
     * already aborted, the read does not start; aborted during it, the result is
     * not handed over. For a file on disk or in memory, that is most of the
     * value a signal has.
     */
    async arrayBuffer(opts) {
        opts?.signal?.throwIfAborted();
        const buffer = await this.blob.arrayBuffer();
        opts?.signal?.throwIfAborted();
        return buffer;
    }
    async bytes(opts) {
        return new Uint8Array(await this.arrayBuffer(opts));
    }
    stream() {
        return this.blob.stream();
    }
    async text(opts) {
        opts?.signal?.throwIfAborted();
        const text = await this.blob.text();
        opts?.signal?.throwIfAborted();
        return text;
    }
}
/**
 * An offset as `Blob.slice` reads one: Web IDL's `[Clamp] long long`, so NaN is
 * 0 and a fraction rounds to the nearest integer, ties to even.
 *
 * Rounded, not truncated. Truncating matched Node 22's `Blob`, which gets it
 * wrong; Node 24's follows the spec, and CI on 24 caught the difference —
 * `slice(1.9, 4.2)` is bytes 2–3, not 1–3.
 */
function toOffset(n) {
    if (Number.isNaN(n))
        return 0;
    if (!Number.isFinite(n))
        return n;
    const floor = Math.floor(n);
    const diff = n - floor;
    if (diff < 0.5)
        return floor;
    if (diff > 0.5)
        return floor + 1;
    return floor % 2 === 0 ? floor : floor + 1;
}
/**
 * A `CsFile` over a range reader — the HTTP case.
 *
 * Slicing composes by arithmetic rather than by fetching, so
 * `file.slice(a, b).slice(c, d)` costs nothing until something is read. That
 * matters for archives: a zip reader slices its way to a central directory
 * through several layers before touching the network once.
 */
export class RangeFile {
    total;
    mime;
    start;
    end;
    path;
    name;
    source;
    constructor(path, total, source, mime = "", 
    /** Window into the underlying object: `[start, end)`. */
    start = 0, end = total) {
        this.total = total;
        this.mime = mime;
        this.start = start;
        this.end = end;
        this.path = path;
        this.name = basename(path.split("#").at(-1) ?? path);
        this.source = typeof source === "function" ? { read: source } : source;
    }
    get size() {
        return Math.max(0, this.end - this.start);
    }
    get type() {
        return this.mime;
    }
    slice(start = 0, end = this.size) {
        // Clamp like `Blob.slice`: negative offsets count from the end, and
        // over-long ranges truncate. A zip reader asking for the last 64 KB of a
        // 20 KB file must get 20 KB rather than an error. Offsets are made whole
        // first, as a `Blob` makes them: NaN let through made the size NaN, and a
        // fraction reached the reader as a `Range` header no host accepts.
        start = toOffset(start);
        end = toOffset(end);
        const size = this.size;
        const from = start < 0 ? Math.max(0, size + start) : Math.min(start, size);
        const to = end < 0 ? Math.max(0, size + end) : Math.min(end, size);
        const lo = this.start + from;
        const hi = this.start + Math.max(from, to);
        return new RangeFile(this.path, this.total, this.source, this.mime, lo, hi);
    }
    async bytes(opts) {
        opts?.signal?.throwIfAborted();
        if (this.size === 0)
            return new Uint8Array(0);
        return await this.source.read(this.start, this.end, opts?.signal);
    }
    async arrayBuffer(opts) {
        const bytes = await this.bytes(opts);
        // A fresh buffer: the view may be a window into a larger one, and handing
        // that out would expose bytes the caller did not ask for. Even a view that
        // is its whole buffer is copied, because that buffer may be one a reader
        // keeps — `bytesFile`'s, or a cached body — and a caller writing into what
        // `Blob.arrayBuffer` promises is theirs would change later reads.
        return bytes.slice().buffer;
    }
    stream() {
        if (this.size === 0) {
            return new ReadableStream({
                start(controller) {
                    controller.close();
                },
            });
        }
        if (this.source.stream)
            return this.source.stream(this.start, this.end);
        // One chunk: the correct-but-simple fallback for a source that cannot
        // stream.
        const self = this;
        const cancel = new AbortController();
        return new ReadableStream({
            async pull(controller) {
                controller.enqueue(await self.bytes({ signal: cancel.signal }));
                controller.close();
            },
            cancel(reason) {
                cancel.abort(reason);
            },
        });
    }
    async text(opts) {
        return new TextDecoder().decode(await this.bytes(opts));
    }
}
/** A `CsFile` over bytes already in memory. */
export function bytesFile(path, data, mime = "") {
    return new RangeFile(path, data.byteLength, async (start, end) => data.subarray(start, end), mime);
}
/**
 * A `CsFile` over a `Blob` or a `File`.
 *
 * The one-liner for the common case: a file the user picked, dropped, or chose
 * with `<input type="file">`. The path defaults to the file's own name so a
 * caller passing a `File` needs nothing else.
 */
export function blobFile(blob, path, mime) {
    const resolved = path ?? `/${blob.name ?? "file"}`;
    return new BlobFile(resolved, blob, mime);
}
/**
 * A `Blob` from bytes, with the type set.
 *
 * Here rather than in each caller because every consumer meets the same
 * friction: TypeScript will not accept a `Uint8Array<ArrayBufferLike>` as a
 * `BlobPart`, since the buffer *might* be a `SharedArrayBuffer`. It never is,
 * for bytes that came out of a file, so the cast belongs at one boundary
 * instead of being rediscovered at every call site.
 *
 * Setting the type matters: an `<img>` will sniff a typeless blob and cope,
 * but an `<iframe>` handed a typeless PDF offers a download rather than
 * rendering it.
 */
export function toBlob(bytes, type = "application/octet-stream") {
    return new Blob([bytes], { type });
}
/**
 * An object URL for a file's contents.
 *
 * **The caller must revoke it.** A page that mints one per image and never
 * revokes pins every image it has ever shown in memory.
 */
export async function objectUrl(file) {
    return URL.createObjectURL(toBlob(await file.bytes(), file.type));
}
