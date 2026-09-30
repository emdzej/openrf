/**
 * The contract every backend implements.
 *
 * Two design decisions shape all of this, and both are worth stating because
 * the obvious alternatives are worse.
 *
 * **A file is modelled on `Blob`, not on `readFile`.** `size`, `slice`,
 * `arrayBuffer`, `stream`, `text` — the same five things a `Blob` gives you.
 * That is not imitation for its own sake: reading *part* of a file is the
 * operation that makes remote data usable at all. An archive is read from its
 * end, a sorted index is binary-searched, a media file is seeked. An interface
 * whose only read is "give me the whole thing" forces a download per lookup,
 * and every backend then grows its own private range API. Because `Blob` and
 * `File` already satisfy this shape, the local backends need no adapter at all.
 *
 * **Reads only.** Writing is a separate, optional interface
 * (`WritableFileSystem`), because `http` and archives cannot write and a
 * combined interface would make every consumer check capabilities it does not
 * use. A read-only filesystem is the common case and should be the plain one.
 */
/** True when a filesystem can be written to. */
export function isWritable(fs) {
    const candidate = fs;
    return (typeof candidate.write === "function" &&
        typeof candidate.makeDirectory === "function" &&
        typeof candidate.remove === "function");
}
