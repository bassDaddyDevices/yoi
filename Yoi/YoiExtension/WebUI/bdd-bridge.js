// bdd-bridge.js
// Bass Daddy Devices plug-in bridge: how an editor page talks to the plug-in hosting it.
//
// The page never talks to a plug-in format directly. It calls the functions on `window.bdd`, and
// the plug-in answers by calling `window.bdd.receive(state)`. In the Audio Unit the messages go
// through WebKit's message handler named "bdd"; the VST3 build binds the same names through its
// web view. With no plug-in at all (the page opened straight in a browser) messages go to
// `window.bddStandIn` if one is loaded, so the page can be worked on without a host.
//
// Nothing here knows about YOI; every Bass Daddy Devices synth can use it as is.

(function () {
    'use strict';

    const webkit = window.webkit && window.webkit.messageHandlers && window.webkit.messageHandlers.bdd;
    const listeners = [];

    function post(message) {
        if (webkit) {
            webkit.postMessage(message);
        } else if (window.bddStandIn) {
            window.bddStandIn.handle(message);
        }
    }

    // Errors on the page go to the plug-in's log too; inside a host there's no console to see.
    window.addEventListener('error', (event) => {
        if (webkit) {
            webkit.postMessage({ type: 'error', message: String(event.message), source: String(event.filename || ''), line: event.lineno || 0 });
        }
    });
    window.addEventListener('unhandledrejection', (event) => {
        if (webkit) {
            webkit.postMessage({ type: 'error', message: String(event.reason), source: '', line: 0 });
        }
    });

    const api = {
        /** True inside a plug-in, false in a plain browser. */
        get connected() {
            return Boolean(webkit);
        },

        /** Calls `listener(state)` with everything the plug-in sends. */
        onReceive(listener) {
            listeners.push(listener);
        },

        /** Called by the plug-in. */
        receive(state) {
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
    };

    // The other bdd-*.js files hang their parts off the same object, whatever order they load in.
    window.bdd = Object.defineProperties(window.bdd || {}, Object.getOwnPropertyDescriptors(api));
})();
