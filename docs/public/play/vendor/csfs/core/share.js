/**
 * One piece of work, several readers, each able to give up.
 *
 * A backend shares expensive work between concurrent readers — one download
 * of a file that several slices want, one inflation of an entry that several
 * reads want. Cancelling that work because *one* reader lost interest would
 * fail the others, and never cancelling it would keep a 945 MB download going
 * after everyone who wanted it has left. So it is cancelled when the last
 * reader holding a signal aborts, and never while a reader without one is
 * waiting.
 */
/** Settle with the promise, or reject with the signal's reason, whichever is first. */
export function untilAborted(promise, signal) {
    if (!signal)
        return promise;
    if (signal.aborted)
        return Promise.reject(signal.reason);
    return new Promise((resolve, reject) => {
        const onAbort = () => reject(signal.reason);
        signal.addEventListener("abort", onAbort, { once: true });
        promise.then((value) => {
            signal.removeEventListener("abort", onAbort);
            resolve(value);
        }, (error) => {
            signal.removeEventListener("abort", onAbort);
            reject(error);
        });
    });
}
/**
 * Wrap work so that concurrent callers share one run of it.
 *
 * A success is kept, so later callers get it at once. A failure is not, and
 * neither is a run abandoned by every caller: the next call starts again.
 */
export function shared(start) {
    let run;
    const begin = () => {
        const controller = new AbortController();
        const r = {
            promise: start(controller.signal),
            controller,
            waiting: 0,
            pinned: false,
            settled: false,
        };
        r.promise.then(() => {
            r.settled = true;
        }, () => {
            r.settled = true;
            if (run === r)
                run = undefined;
        });
        return r;
    };
    return (signal) => {
        if (signal?.aborted)
            return Promise.reject(signal.reason);
        const r = (run ??= begin());
        if (!signal) {
            r.pinned = true;
            return r.promise;
        }
        r.waiting += 1;
        return new Promise((resolve, reject) => {
            const onAbort = () => {
                reject(signal.reason);
                r.waiting -= 1;
                if (r.waiting === 0 && !r.pinned && !r.settled) {
                    r.controller.abort(signal.reason);
                    if (run === r)
                        run = undefined;
                }
            };
            signal.addEventListener("abort", onAbort, { once: true });
            r.promise.then((value) => {
                signal.removeEventListener("abort", onAbort);
                resolve(value);
            }, (error) => {
                signal.removeEventListener("abort", onAbort);
                reject(error);
            });
        });
    };
}
