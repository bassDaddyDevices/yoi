// make-test-vector.mjs
// Writes Tests/fixtures/license-vector.txt: a throwaway test key's public half (hex) and a license
// signed with it by the server's own signing code. Tests/bdd_license_tests.cpp checks that license
// with the plug-ins' C++ checker, proving the two sides agree. The test key is discarded; it
// unlocks nothing real.
//
//     node scripts/make-test-vector.mjs

import { writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

import { signLicense } from '../src/license.js';

const pair = await crypto.subtle.generateKey({ name: 'Ed25519' }, true, ['sign', 'verify']);
const raw = new Uint8Array(await crypto.subtle.exportKey('raw', pair.publicKey));
const license = await signLicense(pair.privateKey, {
    products: ['YOI'],
    licensee: 'Vector Test',
    email: 'vector@example.com',
    id: 'gumroad:test-vector',
    issued: '2026-10-08',
    major: 1,
});

const hex = Array.from(raw, (byte) => byte.toString(16).padStart(2, '0')).join('');
const file = fileURLToPath(new URL('../../../Tests/fixtures/license-vector.txt', import.meta.url));
writeFileSync(file, `${hex}\n${license}\n`);
console.log(`Wrote ${file}`);
