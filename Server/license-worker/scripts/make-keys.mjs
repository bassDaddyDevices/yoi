// make-keys.mjs
// Makes Bass Daddy Devices' license signing key pair. Run once, before release.
//
// On the server (recommended: the private key is born there and never leaves it):
//
//     sudo node scripts/make-keys.mjs --env-file /etc/bdd-license.env
//
//   writes LICENSE_PRIVATE_KEY into that file (permissions 600), refuses to replace a key that's
//   already there, and prints only the PUBLIC key, as the C++ line for
//   Yoi/YoiExtension/Licensing/BDDLicenseKey.hpp.
//
// Anywhere else:
//
//     node scripts/make-keys.mjs
//
//   prints both: the PRIVATE key (for `npx wrangler secret put LICENSE_PRIVATE_KEY`, or the env
//   file) and the public key line.
//
// Keep one copy of the private key offline (a password manager). Never commit it. Anyone with it
// can make licenses; making a new pair invalidates every license already issued.

import { chmodSync, existsSync, readFileSync, writeFileSync } from 'node:fs';

import { base64url } from '../src/license.js';

const envFileIndex = process.argv.indexOf('--env-file');
const envFile = envFileIndex >= 0 ? process.argv[envFileIndex + 1] : null;
if (envFileIndex >= 0 && !envFile) {
    console.error('usage: node scripts/make-keys.mjs [--env-file /etc/bdd-license.env]');
    process.exit(1);
}

const existing = envFile && existsSync(envFile) ? readFileSync(envFile, 'utf8') : '';
if (/^LICENSE_PRIVATE_KEY=\S+/m.test(existing)) {
    console.error(`${envFile} already has a LICENSE_PRIVATE_KEY. Replacing it would invalidate every license`
        + ' already issued, so this stops. Remove the line by hand if you really mean to start again.');
    process.exit(1);
}

const pair = await crypto.subtle.generateKey({ name: 'Ed25519' }, true, ['sign', 'verify']);
const pkcs8 = new Uint8Array(await crypto.subtle.exportKey('pkcs8', pair.privateKey));
const raw = new Uint8Array(await crypto.subtle.exportKey('raw', pair.publicKey));
const privateBase64 = btoa(String.fromCharCode(...pkcs8));
const cpp = Array.from(raw, (byte) => '0x' + byte.toString(16).padStart(2, '0')).join(', ');

if (envFile) {
    const line = `LICENSE_PRIVATE_KEY=${privateBase64}`;
    const updated = /^LICENSE_PRIVATE_KEY=\s*$/m.test(existing)
        ? existing.replace(/^LICENSE_PRIVATE_KEY=\s*$/m, line)
        : existing + (existing && !existing.endsWith('\n') ? '\n' : '') + line + '\n';
    writeFileSync(envFile, updated, { mode: 0o600 });
    chmodSync(envFile, 0o600);
    console.log(`Wrote the private key into ${envFile} (permissions 600). Back it up from there into a`
        + ` password manager (sudo grep LICENSE_PRIVATE_KEY ${envFile}), once, and nowhere else.`);
} else {
    console.log('PRIVATE key (keep offline, never commit):');
    console.log(privateBase64);
}
console.log('');
console.log('PUBLIC key, for Yoi/YoiExtension/Licensing/BDDLicenseKey.hpp:');
console.log(`inline constexpr std::array<uint8_t, 32> kPublicKey { ${cpp} };`);
console.log('');
console.log(`(public key, base64url: ${base64url(raw)})`);
