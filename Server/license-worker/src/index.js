// index.js
// auth.bass-daddy.com: trades a Gumroad license key for a signed Bass Daddy Devices license, once
// (YOI_DOCS/decisions/licensing.md). A Cloudflare Worker.
//
//   POST /activate   { product: "YOI", key: "<Gumroad license key>" }
//                    -> 200 { license, licensee, email }   or   4xx { error }
//   GET  /           a page that does the same in a browser, for offline studio machines
//
// Configuration (wrangler.toml and secrets):
//   PRODUCTS             JSON: { "YOI": { "gumroadProductId": "...", "major": 1, "maxActivations": 5 } }
//   LICENSE_PRIVATE_KEY  secret: the Ed25519 private key, PKCS#8 base64 (scripts/make-keys.mjs)
//
// Gumroad's verify endpoint is public (no API token). Each call can count a "use"; this checks the
// count first without counting, and only counts when it is about to issue a license.

import { importPrivateKey, signLicense } from './license.js';
import { PAGE } from './page.js';

const GUMROAD_VERIFY = 'https://api.gumroad.com/v2/licenses/verify';

// The plug-ins' pages don't come from an ordinary website (choc's custom scheme, the AU's file
// URL), and nothing here uses cookies, so any origin may call.
const CORS = {
    'Access-Control-Allow-Origin': '*',
    'Access-Control-Allow-Methods': 'POST, OPTIONS',
    'Access-Control-Allow-Headers': 'Content-Type',
    'Access-Control-Max-Age': '86400',
};

function json(status, body) {
    return new Response(JSON.stringify(body), {
        status,
        headers: { 'Content-Type': 'application/json; charset=utf-8', 'Cache-Control': 'no-store', ...CORS },
    });
}

async function verifyWithGumroad(fetchImpl, productId, key, count) {
    const body = new URLSearchParams({ product_id: productId, license_key: key, increment_uses_count: count ? 'true' : 'false' });
    const response = await fetchImpl(GUMROAD_VERIFY, { method: 'POST', body });
    // Gumroad answers 404 with { success: false } for an unknown key.
    const data = await response.json().catch(() => ({ success: false }));
    return data;
}

export async function activate(request, env, fetchImpl = fetch, today = new Date()) {
    let input;
    try {
        input = await request.json();
    } catch {
        return json(400, { error: 'Send JSON: { "product": "YOI", "key": "..." }.' });
    }
    const productName = String(input?.product ?? '').trim();
    const key = String(input?.key ?? '').trim();

    let products;
    try {
        products = JSON.parse(env.PRODUCTS || '{}');
    } catch {
        return json(500, { error: 'The license server is misconfigured.' });
    }
    const product = products[productName];
    if (!product || !product.gumroadProductId) {
        return json(404, { error: `Unknown product "${productName}".` });
    }
    if (!key || key.length > 200) {
        return json(400, { error: 'Enter the license key from your Gumroad receipt.' });
    }
    if (!env.LICENSE_PRIVATE_KEY) {
        return json(500, { error: 'The license server is misconfigured.' });
    }

    let check;
    try {
        check = await verifyWithGumroad(fetchImpl, product.gumroadProductId, key, false);
    } catch {
        return json(502, { error: "Couldn't reach Gumroad. Try again in a minute." });
    }
    if (!check.success || !check.purchase) {
        return json(404, { error: `That key wasn't found for ${productName}. Check it against your Gumroad receipt.` });
    }
    const purchase = check.purchase;
    if (purchase.refunded || purchase.disputed || purchase.chargebacked) {
        return json(403, { error: 'That purchase was refunded or disputed, so it no longer has a license.' });
    }
    const maxActivations = Number(product.maxActivations) || 5;
    if (Number(check.uses) >= maxActivations) {
        return json(403, {
            error: `That key has been activated ${check.uses} times, the most allowed. Email support to reset it.`,
        });
    }

    // Count this activation, then sign.
    try {
        const counted = await verifyWithGumroad(fetchImpl, product.gumroadProductId, key, true);
        if (!counted.success) {
            return json(404, { error: `That key wasn't found for ${productName}.` });
        }
    } catch {
        return json(502, { error: "Couldn't reach Gumroad. Try again in a minute." });
    }

    const fields = {
        products: [productName],
        licensee: purchase.full_name || purchase.purchaser_name || purchase.email || '',
        email: purchase.email || '',
        id: `gumroad:${purchase.sale_id || purchase.id || ''}`,
        issued: today.toISOString().slice(0, 10),
        major: product.major || 1,
    };
    const privateKey = await importPrivateKey(env.LICENSE_PRIVATE_KEY);
    const license = await signLicense(privateKey, fields);
    return json(200, { license, licensee: fields.licensee, email: fields.email });
}

export default {
    async fetch(request, env) {
        const url = new URL(request.url);
        if (request.method === 'OPTIONS') {
            return new Response(null, { status: 204, headers: CORS });
        }
        if (url.pathname === '/activate' && request.method === 'POST') {
            return activate(request, env);
        }
        if (url.pathname === '/' && request.method === 'GET') {
            return new Response(PAGE, { headers: { 'Content-Type': 'text/html; charset=utf-8' } });
        }
        return new Response('Not found', { status: 404 });
    },
};
