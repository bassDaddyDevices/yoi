// bdd-bridge.js
// Bass Daddy Devices plug-in bridge: how an editor page talks to the plug-in hosting it.
//
// The page never talks to a plug-in format directly. It calls the functions on `window.bdd`, and
// the plug-in answers by calling `window.bdd.receive(state)`. In the Audio Unit the messages go
// through WebKit's message handler named "bdd"; the VST3 build binds one function, `bddPost`,
// through choc's web view, and takes the same messages. With no plug-in at all (the page opened
// straight in a browser) messages go to `window.bddStandIn` if one is loaded, so the page can be
// worked on without a host.
//
// Nothing here knows about YOI; every Bass Daddy Devices synth can use it as is.

(function () {
    'use strict';

    const webkit = window.webkit && window.webkit.messageHandlers && window.webkit.messageHandlers.bdd;
    // choc defines its bindings before any of the page's scripts run.
    const bound = typeof window.bddPost === 'function' ? window.bddPost : null;
    const native = webkit ? (message) => webkit.postMessage(message) : bound;
    const listeners = [];
    const clipboardRequests = new Map();
    let clipboardToken = 0;

    function post(message) {
        if (native) {
            native(message);
        } else if (window.bddStandIn) {
            window.bddStandIn.handle(message);
        }
    }

    // Errors on the page go to the plug-in's log too; inside a host there's no console to see.
    window.addEventListener('error', (event) => {
        if (native) {
            native({ type: 'error', message: String(event.message), source: String(event.filename || ''), line: event.lineno || 0 });
        }
    });
    window.addEventListener('unhandledrejection', (event) => {
        if (native) {
            native({ type: 'error', message: String(event.reason), source: '', line: 0 });
        }
    });

    const api = {
        /** True inside a plug-in, false in a plain browser. */
        get connected() {
            return Boolean(native);
        },

        /** Calls `listener(state)` with everything the plug-in sends. */
        onReceive(listener) {
            listeners.push(listener);
        },

        /** Called by the plug-in. */
        receive(state) {
            // An answer to readClipboard(): { clipboard: { token, text } }.
            if (state && state.clipboard) {
                const resolve = clipboardRequests.get(state.clipboard.token);
                if (resolve) {
                    clipboardRequests.delete(state.clipboard.token);
                    resolve(String(state.clipboard.text || ''));
                }
            }
            for (const listener of listeners) {
                try {
                    listener(state);
                } catch (error) {
                    console.error(error);
                }
            }
        },

        /** Asks for the full state. Call once the page is ready. */
        hello() {
            post({ type: 'hello' });
        },

        /** A gesture on a parameter: begin, any number of edits, end. Hosts record automation from these. */
        beginEdit(id) {
            post({ type: 'beginEdit', id });
        },
        edit(id, value) {
            post({ type: 'edit', id, value });
        },
        endEdit(id) {
            post({ type: 'endEdit', id });
        },

        /** Sends a new drawing, as [[x, y, bend], ...]. The plug-in answers with the curve it will play. */
        setCurve(points) {
            post({ type: 'setCurve', points });
        },

        /** Loads a factory drawing by index. */
        loadShape(index) {
            post({ type: 'loadShape', index });
        },

        /** The user's own drawings, kept apart from presets: loading one changes only the drawing. */
        requestDrawings() {
            post({ type: 'drawingList' });
        },
        loadDrawing(name) {
            post({ type: 'drawingLoad', name });
        },
        saveDrawing(name) {
            post({ type: 'drawingSave', name });
        },
        deleteDrawing(name) {
            post({ type: 'drawingDelete', name });
        },

        /** Preset commands; metadata only, the complete state stays native to the AU host. */
        requestPresets() {
            post({ type: 'presetList' });
        },
        selectPreset(number) {
            post({ type: 'presetSelect', number });
        },
        savePreset(name) {
            post({ type: 'presetSave', name });
        },
        deletePreset(number) {
            post({ type: 'presetDelete', number });
        },

        /** Called by the plug-in to check the page is alive (after its window has been hidden a
         *  while, for example); the page answers straight away. */
        ping(token) {
            post({ type: 'pong', token });
        },

        /** The clipboard's text, read by the plug-in. Inside a host, ⌘V/Ctrl+V usually goes to the
         *  host's Edit menu rather than the page, so text fields can't paste on their own; this
         *  reads it natively instead. Resolves with '' if there's no text (or no plug-in). */
        readClipboard() {
            if (!native) {
                return navigator.clipboard && navigator.clipboard.readText
                    ? navigator.clipboard.readText().catch(() => '') : Promise.resolve('');
            }
            const token = ++clipboardToken;
            return new Promise((resolve) => {
                clipboardRequests.set(token, resolve);
                post({ type: 'clipboardRead', token });
                // Never leave a caller waiting if the plug-in doesn't answer.
                setTimeout(() => {
                    if (clipboardRequests.delete(token)) resolve('');
                }, 3000);
            });
        },

        /** Hands a signed license (one "BDD1." line) to the plug-in, which checks it itself and
         *  answers with `licenseState` and a `status`. The page can't fake one. */
        installLicense(license) {
            post({ type: 'licenseInstall', license: String(license).trim() });
        },

        /** Activates `product` from whatever the user pasted: a signed license is installed as it
         *  is; anything else is taken to be a store key and traded for a license at the license
         *  server, once (YOI_DOCS/decisions/licensing.md). Resolves when the license has been handed
         *  over; rejects with a message to show. */
        async activate(product, text) {
            const pasted = String(text || '').trim();
            if (!pasted) {
                throw new Error('Paste the license key from your receipt.');
            }
            if (pasted.startsWith('BDD1.')) {
                api.installLicense(pasted);
                return;
            }
            let response;
            try {
                response = await fetch(LICENSE_SERVER + '/activate', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ product, key: pasted }),
                });
            } catch (error) {
                throw new Error('Couldn’t reach ' + LICENSE_SERVER.replace('https://', '')
                    + '. On a computer without internet, get a license file there from another one.');
            }
            const data = await response.json().catch(() => ({}));
            if (!response.ok || !data.license) {
                throw new Error(data.error || 'Activation failed. Try again in a minute.');
            }
            api.installLicense(data.license);
        },
    };

    /** Where store keys are traded for licenses. */
    const LICENSE_SERVER = 'https://auth.bass-daddy.com';

    // The other bdd-*.js files hang their parts off the same object, whatever order they load in.
    window.bdd = Object.defineProperties(window.bdd || {}, Object.getOwnPropertyDescriptors(api));
})();
