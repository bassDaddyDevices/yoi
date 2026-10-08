// activate.test.mjs
// The /activate endpoint against a stand-in for Gumroad. Run with:  npm test  (node --test)

import assert from 'node:assert/strict';
import test from 'node:test';

import { activate } from '../src/index.js';
import { fromBase64 } from '../src/license.js';

const pair = await crypto.subtle.generateKey({ name: 'Ed25519' }, true, ['sign', 'verify']);
const pkcs8 = new Uint8Array(await crypto.subtle.exportKey('pkcs8', pair.privateKey));

const env = {
    PRODUCTS: JSON.stringify({ YOI: { gumroadProductId: 'yoi-product-id', major: 1, maxActivations: 3 } }),
    LICENSE_PRIVATE_KEY: btoa(String.fromCharCode(...pkcs8)),
};

/** A stand-in for Gumroad's verify endpoint, recording what it was asked. */
function fakeGumroad({ known = true, uses = 0, purchase = {} } = {}) {
    const calls = [];
    const fetchImpl = async (url, options) => {
        const form = new URLSearchParams(options.body);
        calls.push({ url, productId: form.get('product_id'), key: form.get('license_key'), count: form.get('increment_uses_count') });
        if (!known || form.get('license_key') !== 'GOOD-KEY') {
            return new Response(JSON.stringify({ success: false, message: 'That license does not exist.' }), { status: 404 });
        }
        if (form.get('increment_uses_count') === 'true') {
            uses += 1;
        }
        return new Response(JSON.stringify({
            success: true,
            uses,
            purchase: { sale_id: 'sale-1', email: 'buyer@example.com', full_name: 'Buyer Name', refunded: false,
                        disputed: false, chargebacked: false, ...purchase },
        }));
    };
    return { fetchImpl, calls };
}

const request = (body) => new Request('https://auth.bass-daddy.com/activate', { method: 'POST', body: JSON.stringify(body) });
const today = new Date('2026-10-08T12:00:00Z');

async function verifyToken(token) {
    const [prefix, payload, signature] = token.split('.');
    assert.equal(prefix, 'BDD1');
    const ok = await crypto.subtle.verify({ name: 'Ed25519' }, pair.publicKey, fromBase64(signature), fromBase64(payload));
    return { ok, text: new TextDecoder().decode(fromBase64(payload)) };
}

test('a good key gets a signed license and counts one use', async () => {
    const gumroad = fakeGumroad();
    const response = await activate(request({ product: 'YOI', key: 'GOOD-KEY' }), env, gumroad.fetchImpl, today);
    assert.equal(response.status, 200);
    const body = await response.json();
    const { ok, text } = await verifyToken(body.license);
    assert.ok(ok, 'the signature should verify');
    assert.equal(text, 'format=bdd-license-1\nproducts=YOI\nlicensee=Buyer Name\nemail=buyer@example.com\n'
        + 'id=gumroad:sale-1\nissued=2026-10-08\nmajor=1\n');
    assert.deepEqual(gumroad.calls.map((call) => call.count), ['false', 'true'], 'check first, then count');
    assert.ok(gumroad.calls.every((call) => call.productId === 'yoi-product-id'));
});

test('an unknown key is refused without counting', async () => {
    const gumroad = fakeGumroad();
    const response = await activate(request({ product: 'YOI', key: 'WRONG' }), env, gumroad.fetchImpl, today);
    assert.equal(response.status, 404);
    assert.equal(gumroad.calls.length, 1);
});

test('refunded and disputed sales are refused', async () => {
    for (const flag of ['refunded', 'disputed', 'chargebacked']) {
        const gumroad = fakeGumroad({ purchase: { [flag]: true } });
        const response = await activate(request({ product: 'YOI', key: 'GOOD-KEY' }), env, gumroad.fetchImpl, today);
        assert.equal(response.status, 403, flag);
        assert.equal(gumroad.calls.length, 1, `${flag}: nothing should be counted`);
    }
});

test('activations stop at the limit', async () => {
    const gumroad = fakeGumroad({ uses: 3 });
    const response = await activate(request({ product: 'YOI', key: 'GOOD-KEY' }), env, gumroad.fetchImpl, today);
    assert.equal(response.status, 403);
    assert.match((await response.json()).error, /activated 3 times/);
});

test('unknown products and bad input are refused', async () => {
    const gumroad = fakeGumroad();
    assert.equal((await activate(request({ product: 'NOPE', key: 'GOOD-KEY' }), env, gumroad.fetchImpl, today)).status, 404);
    assert.equal((await activate(request({ product: 'YOI', key: '' }), env, gumroad.fetchImpl, today)).status, 400);
    const broken = new Request('https://auth.bass-daddy.com/activate', { method: 'POST', body: 'not json' });
    assert.equal((await activate(broken, env, gumroad.fetchImpl, today)).status, 400);
    assert.equal(gumroad.calls.length, 0, 'none of these should reach Gumroad');
});

test('line breaks in Gumroad fields cannot add payload lines', async () => {
    const gumroad = fakeGumroad({ purchase: { full_name: 'Evil\nproducts=EVERYTHING' } });
    const body = await (await activate(request({ product: 'YOI', key: 'GOOD-KEY' }), env, gumroad.fetchImpl, today)).json();
    const { text } = await verifyToken(body.license);
    assert.equal(text.split('\n').filter((line) => line.startsWith('products=')).length, 1);
    assert.match(text, /licensee=Evil products=EVERYTHING\n/);
});
