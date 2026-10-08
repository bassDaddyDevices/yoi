// page.js
// The browser page at auth.bass-daddy.com, for activating a machine that's never online: enter the
// Gumroad key here, then copy the license or download it as a .bddlicense file and drop it into the
// synth's SETTINGS on the offline machine.

export const PAGE = `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Bass Daddy Devices · Licenses</title>
<style>
  :root { color-scheme: dark; --bg: #18191b; --panel: #222427; --text: #e8e6e3; --muted: #9a9894; --accent: #f2a33a; }
  body { margin: 0; min-height: 100vh; display: grid; place-items: center; background: var(--bg); color: var(--text);
         font: 15px/1.5 system-ui, -apple-system, "Segoe UI", sans-serif; }
  main { width: min(560px, 92vw); background: var(--panel); border-radius: 14px; padding: 32px; }
  h1 { margin: 0 0 4px; font-size: 20px; letter-spacing: 0.04em; }
  p { color: var(--muted); margin: 0 0 20px; }
  label { display: block; font-size: 12px; letter-spacing: 0.08em; text-transform: uppercase; color: var(--muted); margin-bottom: 6px; }
  input, select, textarea { width: 100%; box-sizing: border-box; background: #111214; color: var(--text); border: 1px solid #34363a;
         border-radius: 8px; padding: 10px 12px; font: inherit; margin-bottom: 16px; }
  textarea { font: 12px/1.4 ui-monospace, Menlo, monospace; min-height: 96px; resize: vertical; }
  button { background: var(--accent); color: #18191b; border: 0; border-radius: 8px; padding: 10px 18px; font: inherit;
           font-weight: 600; cursor: pointer; }
  button[disabled] { opacity: 0.5; cursor: default; }
  .row { display: flex; gap: 10px; flex-wrap: wrap; }
  .status { min-height: 1.5em; margin: 12px 0 0; }
  .error { color: #ff7a6b; }
  #result { display: none; margin-top: 20px; }
</style>
</head>
<body>
<main>
  <h1>Activate a license</h1>
  <p>For a computer without internet. On a connected computer, just paste your key into the synth's SETTINGS instead.</p>
  <form id="form">
    <label for="product">Synth</label>
    <select id="product"><option value="YOI">YOI</option></select>
    <label for="key">License key from your Gumroad receipt</label>
    <input id="key" autocomplete="off" spellcheck="false" required>
    <button id="go" type="submit">Get license</button>
    <div id="status" class="status"></div>
  </form>
  <section id="result">
    <label for="license">Your license</label>
    <textarea id="license" readonly></textarea>
    <div class="row">
      <button id="copy" type="button">Copy</button>
      <button id="download" type="button">Download .bddlicense</button>
    </div>
    <p class="status">On the offline computer, open the synth's SETTINGS and paste it, or drop the file in.</p>
  </section>
</main>
<script>
  const form = document.getElementById('form');
  const status = document.getElementById('status');
  const go = document.getElementById('go');
  form.addEventListener('submit', async (event) => {
    event.preventDefault();
    go.disabled = true;
    status.className = 'status';
    status.textContent = 'Checking with Gumroad...';
    try {
      const response = await fetch('/activate', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ product: document.getElementById('product').value, key: document.getElementById('key').value }),
      });
      const data = await response.json();
      if (!response.ok) throw new Error(data.error || 'Something went wrong.');
      document.getElementById('license').value = data.license;
      document.getElementById('result').style.display = 'block';
      status.textContent = 'Licensed to ' + (data.licensee || data.email) + '.';
    } catch (error) {
      status.className = 'status error';
      status.textContent = error.message;
    } finally {
      go.disabled = false;
    }
  });
  document.getElementById('copy').addEventListener('click', () => {
    navigator.clipboard.writeText(document.getElementById('license').value);
    status.textContent = 'Copied.';
  });
  document.getElementById('download').addEventListener('click', () => {
    const blob = new Blob([document.getElementById('license').value + '\\n'], { type: 'text/plain' });
    const link = document.createElement('a');
    link.href = URL.createObjectURL(blob);
    link.download = document.getElementById('product').value + '.bddlicense';
    link.click();
  });
</script>
</body>
</html>
`;
