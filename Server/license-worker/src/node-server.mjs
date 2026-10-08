// node-server.mjs
// Runs the license server under Node (18 or later) on an ordinary server, such as the Spaceship VPS
// that auth.bass-daddy.com already points at, so no DNS change is needed. It serves exactly the
// same code as the Cloudflare Worker (index.js): this file only turns Node's HTTP server into the
// Worker's fetch(request, env).
//
//     PORT=8787 PRODUCTS='{...}' LICENSE_PRIVATE_KEY='...' node src/node-server.mjs
//
// It listens on 127.0.0.1 only: put the VPS's web server (nginx, Caddy, Apache) in front of it for
// HTTPS (deploy/ has examples). Configuration comes from the environment, as on Cloudflare:
//   PRODUCTS, LICENSE_PRIVATE_KEY   as in wrangler.toml / the Worker's secrets
//   PORT (default 8787), HOST (default 127.0.0.1)

import { createServer } from 'node:http';

import worker from './index.js';

const port = Number(process.env.PORT || 8787);
const host = process.env.HOST || '127.0.0.1';
const env = {
    PRODUCTS: process.env.PRODUCTS || '{}',
    LICENSE_PRIVATE_KEY: process.env.LICENSE_PRIVATE_KEY || '',
};

/** Requests are a few hundred bytes; anything much bigger is refused before it's read. */
const MAX_BODY = 16 * 1024;

const server = createServer(async (incoming, outgoing) => {
    try {
        const chunks = [];
        let size = 0;
        for await (const chunk of incoming) {
            size += chunk.length;
            if (size > MAX_BODY) {
                outgoing.writeHead(413).end();
                return;
            }
            chunks.push(chunk);
        }
        // The proxy in front sets the public host and scheme.
        const scheme = incoming.headers['x-forwarded-proto'] || 'http';
        const url = new URL(incoming.url || '/', `${scheme}://${incoming.headers.host || 'localhost'}`);
        const hasBody = incoming.method !== 'GET' && incoming.method !== 'HEAD' && chunks.length > 0;
        const request = new Request(url, {
            method: incoming.method,
            headers: Object.entries(incoming.headers).flatMap(([name, value]) =>
                Array.isArray(value) ? value.map((item) => [name, item]) : value === undefined ? [] : [[name, value]]),
            body: hasBody ? Buffer.concat(chunks) : undefined,
        });

        const response = await worker.fetch(request, env);
        outgoing.writeHead(response.status, Object.fromEntries(response.headers));
        outgoing.end(Buffer.from(await response.arrayBuffer()));
    } catch (error) {
        console.error('license server error:', error);
        if (!outgoing.headersSent) {
            outgoing.writeHead(500, { 'Content-Type': 'application/json' });
        }
        outgoing.end(JSON.stringify({ error: 'The license server had a problem. Try again in a minute.' }));
    }
});

server.listen(port, host, () => {
    if (!env.LICENSE_PRIVATE_KEY) {
        console.warn('LICENSE_PRIVATE_KEY is not set: /activate will refuse every request.');
    }
    console.log(`bdd license server on http://${host}:${port}`);
});
