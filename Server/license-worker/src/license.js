// license.js
// Signs Bass Daddy Devices licenses (YOI_DOCS/decisions/licensing.md). Plain WebCrypto, so the
// same code runs in the Cloudflare Worker, in Node for the scripts and tests, and nowhere needs a
// library.
//
// A license is one line:  BDD1.<payload, base64url>.<Ed25519 signature, base64url>
// The payload is "key=value" lines, checked in the plug-ins by Licensing/BDDLicense.hpp.

export const LICENSE_FORMAT = 'bdd-license-1';

const encoder = new TextEncoder();

export function base64url(bytes) {
    let binary = '';
    for (const byte of bytes) {
        binary += String.fromCharCode(byte);
    }
    return btoa(binary).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
}

export function fromBase64(text) {
    const binary = atob(text.replace(/-/g, '+').replace(/_/g, '/'));
    return Uint8Array.from(binary, (c) => c.charCodeAt(0));
}

/** A value on one payload line: no line breaks, trimmed, and not absurdly long. */
function clean(value) {
    return String(value ?? '').replace(/[\r\n]+/g, ' ').trim().slice(0, 200);
}

/** The payload's exact text. `fields`: products (array), licensee, email, id, issued, major. */
export function makePayload(fields) {
    const lines = [
        `format=${LICENSE_FORMAT}`,
        `products=${fields.products.map(clean).join(',')}`,
        `licensee=${clean(fields.licensee)}`,
        `email=${clean(fields.email)}`,
        `id=${clean(fields.id)}`,
        `issued=${clean(fields.issued)}`,
        `major=${Number.parseInt(fields.major, 10) || 1}`,
    ];
    return lines.join('\n') + '\n';
}

/** The private key from its secret: PKCS#8, base64, as scripts/make-keys.mjs prints it. */
export function importPrivateKey(pkcs8Base64) {
    return crypto.subtle.importKey('pkcs8', fromBase64(pkcs8Base64), { name: 'Ed25519' }, false, ['sign']);
}

export async function signLicense(privateKey, fields) {
    const payload = encoder.encode(makePayload(fields));
    const signature = new Uint8Array(await crypto.subtle.sign({ name: 'Ed25519' }, privateKey, payload));
    return `BDD1.${base64url(payload)}.${base64url(signature)}`;
}
