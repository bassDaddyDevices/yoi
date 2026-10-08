# Setting up auth.bass-daddy.com on the VPS

A brief for whoever sets this up on the Spaceship VPS: the owner, or Claude Code running there. Read the rest of this folder's `README.md` for what the server does. The design is in the YOI docs (`decisions/licensing.md`).

**What's already true:** Node is installed. `auth.bass-daddy.com` already points at this VPS, has HTTPS, and serves nothing else. DNS does not change.

**What we're doing:** running `src/node-server.mjs` (no dependencies) as a systemd service on `127.0.0.1:8787`, and making the existing HTTPS site for `auth.bass-daddy.com` pass requests to it.

## Rules

- **The private key is created here and never leaves this machine.** Never print it in a reply, paste it anywhere, commit it, or copy it off the server. The only exception is the owner's own one-time backup into a password manager.
- **Don't touch DNS, other sites, or other services.** Back up any web-server config before editing it, and test the config before reloading.
- If something doesn't match this brief (no systemd, an unexpected web server, a control panel managing the config), stop and ask the owner rather than improvising.

## Steps

1. **Check Node:** `node --version` must be 18 or later; `node -e "crypto.subtle.generateKey({name:'Ed25519'},true,['sign']).then(()=>console.log('ok'))"` must print `ok`.

2. **Install the files** at `/opt/bdd-license`: this folder's contents, copied from the owner's Mac (`scp -r Server/license-worker <user>@<vps>:/tmp/` then `sudo mv /tmp/license-worker /opt/bdd-license`) or taken from the repo. Then `cd /opt/bdd-license && npm test`: 6 tests should pass.

3. **Configuration:** `sudo cp deploy/bdd-license.env.example /etc/bdd-license.env && sudo chmod 600 /etc/bdd-license.env`. It already holds YOI's Gumroad `product_id` (`Opo8b_oq_j-dAJHEX-zCIg==`, in `PRODUCTS`). Leave `LICENSE_PRIVATE_KEY=` empty: step 4 fills it.

4. **The signing key, once:** `sudo node scripts/make-keys.mjs --env-file /etc/bdd-license.env`. It writes the private key into the env file and prints the **public** key line. Give the owner that `kPublicKey` line, which is safe to share; it goes into YOI. Remind them to back up the private key once (`sudo grep LICENSE_PRIVATE_KEY /etc/bdd-license.env`). The script refuses to run again if a key exists. **Never replace the key**, or every license already issued stops working.

5. **The service:** `sudo cp deploy/bdd-license.service /etc/systemd/system/ && sudo systemctl daemon-reload && sudo systemctl enable --now bdd-license`. Check with `systemctl status bdd-license` and `curl -s -o /dev/null -w '%{http_code}\n' http://127.0.0.1:8787/`, which should print `200`. Logs: `journalctl -u bdd-license`.

6. **The web server:** find which one serves `auth.bass-daddy.com` with HTTPS. Then, in that site's existing HTTPS block, keeping its certificate settings:
   - **nginx:** add the `location` blocks from `deploy/nginx.conf`, and its `limit_req_zone` line in the `http` block. Run `sudo nginx -t`, then `sudo systemctl reload nginx`.
   - **Apache:** enable `proxy` and `proxy_http` (`sudo a2enmod proxy proxy_http`). Add `ProxyPreserveHost On`, `ProxyPass / http://127.0.0.1:8787/`, `ProxyPassReverse / http://127.0.0.1:8787/` and `RequestHeader set X-Forwarded-Proto https`, which needs `a2enmod headers`. Run `sudo apachectl configtest`, then reload.
   - **Caddy:** use `deploy/Caddyfile`.

7. **Check it from outside:**
   - `curl -s -o /dev/null -w '%{http_code}\n' https://auth.bass-daddy.com/` should print `200`. In a browser it shows "Activate a license".
   - `curl -s -X OPTIONS -D - -o /dev/null https://auth.bass-daddy.com/activate | grep -i access-control-allow-origin` should show `*`. The plug-ins depend on this.
   - `curl -s -X POST -H 'Content-Type: application/json' --data '{"product":"YOI","key":"not-a-real-key"}' https://auth.bass-daddy.com/activate` should answer with a JSON error saying the key wasn't found. That proves the request reached Gumroad and came back. (Gumroad gives the same answer for a wrong product ID, so only a real purchase proves the ID; the owner does that last.)

## Report back to the owner

- The `kPublicKey` line, the one thing that goes into YOI.
- The results of steps 5 and 7.
- Which web server it was and what was changed in its config, with the backup's path.

## Later

- **Updating:** copy the new files over `/opt/bdd-license`, without touching `/etc/bdd-license.env`, then `sudo systemctl restart bdd-license`.
- **Support:** to reset a buyer's activations, use Gumroad's Sales dashboard (License key section) rather than this server.
