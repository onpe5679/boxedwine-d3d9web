(function installFaceWisHleWorkerAlertFallback(global) {
    'use strict';

    // Emscripten emits BoxedWine's native message-box EM_ASM callback as a
    // bare alert(...).  Dedicated workers do not provide Window.alert, so the
    // callback used to hide the native diagnostic with a ReferenceError.
    // Keep the browser-main implementation untouched and make candidate
    // worker diagnostics observable without throwing a second exception.
    if (typeof global.alert === 'function') {
        return;
    }
    global.alert = function faceWisHleWorkerAlert(message) {
        var text;
        try {
            text = String(message);
        } catch (_) {
            text = '<unprintable message>';
        }
        if (global.console && typeof global.console.error === 'function') {
            global.console.error('[FACE_WIS_HLE_WORKER_ALERT] ' + text);
        }
    };
})(globalThis);
