# auth.bass-daddy.com: the license server

A small server (written as a Cloudflare Worker, and runnable under Node on the VPS) that trades a Gumroad license key for a signed Bass Daddy Devices license, once. The plug-ins check that license offline from then on. The design is in `YOI_DOCS/decisions/licensing.md`.

```
src/index.js     POST /activate and GET / (the page for offline computers), as a Worker
src/node-server.mjs  the same, under Node, for an ordinary server such as the VPS
deploy/          systemd unit, env template, nginx and Caddy examples for the VPS
src/license.js   the signing code: the BDD1 license format
src/page.js      that page
scripts/         make-keys.mjs (the real key pair, once), make-test-vector.mjs (the C++ cross-check)
test/            /activate against a stand-in for Gumroad
```

No dependencies: plain WebCrypto and fetch, which both Cloudflare and Node 18+ provide. Run the tests with `npm test`. After changing the license format, run `npm run make-test-vector` and the C++ tests in `Tests/`.

## Setting it up (once)

The same code runs in two places. **The Spaceship VPS is the default:** `auth.bass-daddy.com` already points there, so no DNS changes. Cloudflare is the alternative if you ever move the domain's DNS there.

### Before either

1. **Gumroad:** on the YOI product, turn on "Generate a unique license key per sale", and copy the product's `product_id` from the License key block on its Content page.
2. **The signing key, once.** On the VPS, make it there so it never leaves: `sudo node scripts/make-keys.mjs --env-file /etc/bdd-license.env` (see `SERVER-SETUP.md`). For Cloudflare, use `npm run make-keys`. Either way, back the private key up once in a password manager, paste the printed `kPublicKey` line into `Yoi/YoiExtension/Licensing/BDDLicenseKey.hpp`, and set `kLicensingEnforced = true` there. Never commit the private key; a new pair later invalidates every license already issued.

### On the VPS (no DNS change)

`SERVER-SETUP.md` is the step-by-step brief, written so Claude Code on the VPS can follow it.

Needs Node 18 or later (`node --version`) and a web server that can do HTTPS (nginx, Caddy or Apache).

1. Copy this folder to `/opt/bdd-license` (it has no dependencies; nothing to install).
2. `sudo cp deploy/bdd-license.env.example /etc/bdd-license.env`, fill in `PRODUCTS` (the Gumroad `product_id`) and `LICENSE_PRIVATE_KEY`, then `sudo chmod 600 /etc/bdd-license.env`.
3. `sudo cp deploy/bdd-license.service /etc/systemd/system/ && sudo systemctl daemon-reload && sudo systemctl enable --now bdd-license`. Check it with `curl http://127.0.0.1:8787/` and `journalctl -u bdd-license`.
4. Point `auth.bass-daddy.com` at it in the web server: `deploy/nginx.conf` (then `sudo certbot --nginx -d auth.bass-daddy.com` for HTTPS, with a rate limit on `/activate`) or `deploy/Caddyfile` (HTTPS automatic). If the subdomain already serves something, put these lines in its existing server block instead.
5. Open `https://auth.bass-daddy.com` in a browser: the activation page should load.

Updating later: copy the new files over `/opt/bdd-license` and `sudo systemctl restart bdd-license`.

### On Cloudflare (needs bass-daddy.com's DNS on Cloudflare)

Move the DNS (free plan; check every imported record, especially the main site, the VPS and email/MX, before changing the nameservers at Spaceship). Put the `product_id` in `PRODUCTS` in `wrangler.toml`, then `npx wrangler login`, `npx wrangler secret put LICENSE_PRIVATE_KEY`, and `npm run deploy`. Add a rate-limiting rule on `/activate`.

### Either way

Test a real purchase (Gumroad lets you buy your own product at 100 % off): activate in YOI, reopen it, and try the offline page.

## Support jobs

- **"I've used up my activations"**: reset the uses on the sale in Gumroad's Sales dashboard (License key section).
- **Refunds**: refunded or disputed sales can't activate again. Licenses already issued keep working offline; that's deliberate.
