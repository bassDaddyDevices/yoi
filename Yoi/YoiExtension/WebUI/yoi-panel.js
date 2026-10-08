// yoi-panel.js: modern YOI performance panel. Curve maths remains in the C++ kernel;
// this page only edits points and displays the table sent through the shared bridge.
(function () {
    'use strict';

    const { dial, fader, choice, level, valueButton, readout, tabBar, pad, format } = bdd.controls;
    const STAGE_WIDTH = 1040;
    const STAGE_HEIGHT = 640;
    const parameters = new Map();
    const byAddress = new Map();
    const controls = new Map();
    const values = Object.create(null);
    const detailRows = Object.create(null);
    let detailsOpen = false;
    let detailTabs = null;
    let shapeNames = [];
    let built = false;
    let filterGraph = null;
    /// The filter as the kernel last set it: { cutoffHz, filterQ, cutoffTopHz, cutoffBottomHz }.
    let filterState = null;
    let levelControl = null;
    let userPresets = [];
    let factoryPresets = [];
    let currentPreset = null;
    let presetMenu = null;
    let presetDialogMode = null;
    let userDrawings = [];      // names of the user's own drawings, from the plug-in
    let drawingMenu = null;
    let pendingDrawingName = null;   // the drawing a delete dialog is asking about
    /// What the plug-in says about its license: { product, enforced, licensed, message, licensee }.
    /// The plug-in checks licenses; the page only shows the answer and passes on what's pasted.
    let licenseState = null;
    let licenseBusy = false;
    const LICENSE_PAGE = 3;

    // The plug-in also lists the old Accelerate direction (5), for saved sessions, and plays it as
    // Forward with ACCEL on. The panel offers only these five, shows 5 as FORWARD with ACCEL lit,
    // and turns it into those two settings the first time either is touched.
    const DIRECTIONS = ['Forward', 'Backward', 'Pingpong', 'Sine', 'Random'];
    const LEGACY_ACCELERATE = 5;
    const isLegacyAccelerate = () => Math.round(Number(values.envDirection ?? 0)) === LEGACY_ACCELERATE;

    /// Reads a parameter whose meaning the plug-in owns (OTT TIME's release, say) out of the
    /// readings the descriptor sampled across its range. The mapping stays in the kernel.
    function derivedDisplay(item) {
        return (value) => {
            const readings = item.valueDisplays;
            if (!readings || !readings.length) return format(item, value);
            const span = item.max - item.min || 1;
            const fraction = Math.max(0, Math.min(1, (Number(value) - item.min) / span));
            return readings[Math.round(fraction * (readings.length - 1))];
        };
    }

    // Filter graph geometry, shared by the one-off grid and the response redraw: a 300×300 box
    // spanning 20 Hz-20 kHz across and -36…+18 dB up.
    const GRAPH_SIZE = 300;
    const GRAPH_MIN_LOG = Math.log2(20);
    const GRAPH_LOG_SPAN = Math.log2(20000) - GRAPH_MIN_LOG;
    const xForFrequency = (frequency) => GRAPH_SIZE * (Math.log2(frequency) - GRAPH_MIN_LOG) / GRAPH_LOG_SPAN;
    const yForDb = (db) => 276 - (Math.max(-36, Math.min(18, db)) + 36) * 4;

    function parameter(identifier) {
        return parameters.get(identifier);
    }

    function addControl(identifier, control) {
        if (!controls.has(identifier)) controls.set(identifier, []);
        controls.get(identifier).push(control);
        values[identifier] = control.value;
        return control;
    }

    /// A whole gesture on a parameter the user didn't touch directly, shown on the panel at once.
    function setFromPanel(identifier, value) {
        const item = parameter(identifier);
        if (!item) return;
        bdd.beginEdit(item.id);
        bdd.edit(item.id, value);
        bdd.endEdit(item.id);
        values[identifier] = value;
        for (const control of controls.get(identifier) || []) control.set(value);
    }

    function onChange(identifier) {
        return (value) => {
            // Leaving the old Accelerate direction: DIR picked keeps ACCEL on; ACCEL picked sets
            // DIR to the Forward it was playing.
            if (isLegacyAccelerate()) {
                if (identifier === 'envDirection') setFromPanel('envAccelerate', 1);
                if (identifier === 'envAccelerate') setFromPanel('envDirection', 0);
            }
            values[identifier] = value;
            for (const control of controls.get(identifier) || []) {
                if (!control.gesture?.active) control.set(value);
            }
            refresh();
        };
    }

    function makeControl(identifier, factory, options = {}) {
        const item = parameter(identifier);
        if (!item) return null;
        return addControl(identifier, factory(item, Object.assign({}, options, { onChange: onChange(identifier) })));
    }

    function mountControl(container, identifier, factory, options = {}) {
        const control = makeControl(identifier, factory, options);
        if (!control) return null;
        container.appendChild(control.element);
        control.element.dataset.parameter = identifier;
        return control;
    }

    function row(container, identifier, factory, options = {}) {
        const wrapper = document.createElement('div');
        wrapper.className = 'detail-row';
        wrapper.dataset.identifier = identifier;
        const control = mountControl(wrapper, identifier, factory, options);
        if (!control) return null;
        const reason = document.createElement('span');
        reason.className = 'dim-reason';
        wrapper.appendChild(reason);
        container.appendChild(wrapper);
        return { wrapper, control, reason };
    }

    function section(container, title) {
        const block = document.createElement('section');
        block.className = 'detail-section';
        const heading = document.createElement('h3');
        heading.textContent = title;
        block.appendChild(heading);
        container.appendChild(block);
        return block;
    }

    function buildMacros() {
        const host = document.getElementById('macro-row');
        const macros = [
            ['macroVoice', 'VOICE'],
            ['macroPower', 'POWER'],
            ['macroControl', 'CONTROL'],
            ['macroWidth', 'WIDTH'],
        ];
        for (const [identifier, label] of macros) {
            const card = document.createElement('div');
            card.className = 'macro-card bdd-card';
            const control = mountControl(card, identifier, dial, { label, size: 84 });
            if (control) host.appendChild(card);
        }
    }

    function buildDrawingControls() {
        const motion = document.getElementById('motion-controls');
        // The two icon switches side by side at the left, then TIME and DIR in their own group,
        // centred in the space that's left, under the middle of the drawing.
        // Time: a beamed note (sync to the host's tempo) and a stopwatch (a free time in milliseconds).
        mountControl(motion, 'envTimeMode', choice, {
            labels: ['Sync to tempo', 'Free time'],
            vertical: true,
            icons: [
                '<svg viewBox="0 0 16 16" aria-hidden="true"><path d="M6 12.5V3.5l7-1.5v9"/><circle cx="4.3" cy="12.5" r="1.7"/><circle cx="11.3" cy="11" r="1.7"/></svg>',
                '<svg viewBox="0 0 16 16" aria-hidden="true"><circle cx="8" cy="9.5" r="5"/><path d="M8 9.5V6.8M6.3 1.8h3.4M8 1.8v2.7M12.2 4.6l1-1"/></svg>',
            ],
        });
        // ACCEL works with any direction. Evenly spaced ticks (a steady speed) and ticks bunching
        // up (speeding up).
        mountControl(motion, 'envAccelerate', choice, {
            labels: ['Steady speed', 'Accelerate'],
            vertical: true,
            icons: [
                '<svg viewBox="0 0 16 16" aria-hidden="true"><path d="M1.5 8h13M3 5.5v5M8 5.5v5M13 5.5v5"/></svg>',
                '<svg viewBox="0 0 16 16" aria-hidden="true"><path d="M1.5 8h13M2 5.5v5M8 5.5v5M11.5 5.5v5M13.5 5.5v5"/></svg>',
            ],
        });
        const readouts = document.createElement('div');
        readouts.className = 'motion-readouts';
        motion.appendChild(readouts);
        const sync = mountControl(readouts, 'envSyncLength', readout, {
            label: 'TIME:', scale: 2, menuColumns: 4,
            menuItems: (parameter('envSyncLength')?.options || []).map((name) => name.toUpperCase()),
            format: (value) => (parameter('envSyncLength')?.options || [])[Math.round(value)]?.toUpperCase() || '',
        });
        const free = mountControl(readouts, 'envFreeTime', valueButton, {
            label: 'Envelope time in milliseconds', format: (value) => Math.round(value) + ' ms', travel: 260,
        });
        if (sync) sync.element.id = 'time-sync';
        if (free) free.element.id = 'time-free';
        mountControl(readouts, 'envDirection', readout, {
            label: 'DIR:', scale: 2,
            menuItems: DIRECTIONS.map((name) => name.toUpperCase()),
            format: (value) => DIRECTIONS[Math.round(value)]?.toUpperCase() || 'FORWARD',
        });
        mountControl(document.getElementById('amount-control'), 'envAmount', fader, {
            label: 'AMOUNT', inline: false, travel: 170,
        });
    }

    function buildCharacter() {
        const host = document.getElementById('character-pad');
        const throat = parameter('macroThroat');
        const resonance = parameter('resonance');
        if (!throat || !resonance) return;

        const character = pad(throat, resonance, {
            size: 290,   // the card's full width now the two switches share one row
            label: 'Character: throat across, resonance up',
            xLabel: 'THROAT →',
            yLabel: '↑ RESONANCE',
            onChange: (throatValue, resonanceValue) => {
                values.macroThroat = throatValue;
                values.resonance = resonanceValue;
                showCharacter(throatValue, resonanceValue);
                refresh();
            },
        });
        host.appendChild(character.element);
        installFilterGraph(character.element);
        for (const control of character.controls) {
            const identifier = control.parameter.identifier;
            const originalSet = control.set.bind(control);
            control.set = (value) => {
                originalSet(value);
                values[identifier] = value;
                showCharacter(character.controls[0].value, character.controls[1].value);
            };
            addControl(identifier, control);
        }
        showCharacter(character.controls[0].value, character.controls[1].value);

        mountControl(document.getElementById('ds-mode'), 'dsMode', choice, { labels: ['S&H', 'DOWNSAMPLE'], values: [1, 2] });
        mountControl(document.getElementById('ds-lock'), 'dsLock', choice, { labels: ['FREE', 'LOCK'] });
    }

    function showCharacter(throat, resonance) {
        const throatParameter = parameter('macroThroat');
        const resonanceParameter = parameter('resonance');
        document.getElementById('throat-value').textContent = throatParameter ? format(throatParameter, throat) : '';
        document.getElementById('res-value').textContent = resonanceParameter ? format(resonanceParameter, resonance) : '';
    }

    function installFilterGraph(padElement) {
        const art = padElement.querySelector('.bdd-pad-art');
        if (!art) return;
        art.querySelector('.bdd-pad-peak-fill')?.remove();
        art.querySelector('.bdd-pad-peak')?.remove();
        const svgNS = 'http://www.w3.org/2000/svg';
        const makePath = (className) => {
            const path = document.createElementNS(svgNS, 'path');
            path.setAttribute('class', className);
            return path;
        };
        const grid = makePath('bdd-filter-grid');
        const area = makePath('bdd-filter-area');
        const response = makePath('bdd-filter-response');
        const cutoff = makePath('bdd-filter-cutoff');
        art.append(grid, area, response, cutoff);
        filterGraph = { grid, area, response, cutoff };
        const gridLines = [];
        for (const frequency of [20, 40, 80, 160, 320, 640, 1250, 2500, 5000, 10000, 20000]) {
            const x = xForFrequency(frequency).toFixed(2);
            gridLines.push(`M${x} 0V300`);
        }
        for (const db of [-24, -12, 0, 12]) {
            const y = yForDb(db).toFixed(2);
            gridLines.push(`M0 ${y}H300`);
        }
        grid.setAttribute('d', gridLines.join(''));
        // Drawn straight away rather than scheduled: an off-screen page gets no animation frames.
        // These paths are new and empty, so the cached signature must not suppress the first draw.
        filterGraphSignature = '';
        updateFilterGraph();
    }

    /// Coalesces graph redraws into one animation frame and skips the frame entirely when nothing
    /// the curve depends on has moved. Display messages arrive 30 times a second whether or not the
    /// playhead is running, and each redraw samples the response 160 times.
    let filterGraphFrame = 0;
    let filterGraphSignature = '';

    function scheduleFilterGraph() {
        if (!filterGraph || filterGraphFrame) return;
        filterGraphFrame = window.requestAnimationFrame(() => {
            filterGraphFrame = 0;
            updateFilterGraph();
        });
    }

    function updateFilterGraph() {
        if (!filterGraph || !filterState) return;
        // Straight from the kernel: it applies the drawing to CUTOFF and remaps RES to Q, and this
        // page only draws the result. Working either out here would be a second copy of the maths.
        const cutoffHz = Math.max(20, Math.min(20000, Number(filterState.cutoffHz)));
        const q = Math.max(0.5, Number(filterState.filterQ));
        const bandPass = Number(values.filterMode ?? 0) >= 0.5;
        // Nothing below moves unless one of these does, and the path is identical if they haven't.
        const signature = `${cutoffHz.toFixed(2)}|${q.toFixed(4)}|${bandPass ? 1 : 0}`;
        if (signature === filterGraphSignature) return;
        filterGraphSignature = signature;
        const segments = [];
        const sampleCount = 160;
        for (let index = 0; index <= sampleCount; index++) {
            const x = index / sampleCount * GRAPH_SIZE;
            const frequency = Math.pow(2, GRAPH_MIN_LOG + GRAPH_LOG_SPAN * x / GRAPH_SIZE);
            const ratio = frequency / cutoffHz;
            const denominator = Math.sqrt(Math.pow(1 - ratio * ratio, 2) + Math.pow(ratio / q, 2));
            const magnitude = bandPass ? (ratio / q) / denominator : 1 / denominator;
            const db = 20 * Math.log10(Math.max(1e-5, magnitude));
            segments.push(`${index === 0 ? 'M' : 'L'}${x.toFixed(2)} ${yForDb(db).toFixed(2)}`);
        }
        const linePath = segments.join(' ');
        filterGraph.response.setAttribute('d', linePath);
        filterGraph.area.setAttribute('d', `${linePath} L300 300 L0 300 Z`);
        const xCutoff = xForFrequency(cutoffHz).toFixed(2);
        filterGraph.cutoff.setAttribute('d', `M${xCutoff} 0V300`);
        filterGraph.response.setAttribute('data-mode', bandPass ? 'bp' : 'lp');
        filterGraph.cutoff.setAttribute('aria-label', `Current filter cutoff ${Math.round(cutoffHz)} hertz`);
    }

    function buildLevel() {
        const control = mountControl(document.getElementById('level-control'), 'outputLevel', level, { label: 'OUT', travel: 400 });   // about the track's length
        if (control) control.element.classList.add('level-control');
        levelControl = control;
    }

    /// Where a peak (linear, 1 is 0 dBFS) sits on the meter. The meter shares the LEVEL fader's
    /// dB range, so a meter's top lines up with the same dB on the fader beside it.
    function meterHeight(peak) {
        const range = parameter('outputLevel');
        if (!range) return 0;
        const decibels = 20 * Math.log10(Math.max(1e-6, Number(peak) || 0));
        return (decibels - range.min) / (range.max - range.min);
    }

    function buildCutoff() {
        mountControl(document.getElementById('cutoff-dial'), 'cutoff', dial, { label: 'CUTOFF', size: 82 });
        mountControl(document.getElementById('cutoff-mode'), 'filterMode', choice, { labels: ['LP', 'BP'], vertical: true });
    }

    function buildDetails() {
        const voicePage = document.querySelector('[data-page="0"]');
        let group = section(voicePage, 'OSCILLATORS');
        row(group, 'subOctave', choice, { labels: ['−1 OCT', '−2 OCT'] });
        row(group, 'subCrossover', fader, { label: 'SUB CROSSOVER' });
        // The sub swells and dips with how much of the top the filter lets through.
        row(group, 'subFollow', fader, { label: 'SUB FOLLOWS TOP' });

        group = section(voicePage, 'GLIDE & PITCH');
        detailRows.glide = row(group, 'glideTime', fader, { label: 'GLIDE TIME' });
        detailRows.glideMode = row(group, 'glideMode', choice, { labels: ['LEGATO', 'ALWAYS'] });
        row(group, 'bendRange', fader, { label: 'PITCH BEND RANGE' });

        const envelopePage = document.querySelector('[data-page="1"]');
        // Each new note either restarts the drawing (RETRIGGER) or leaves it running (FREE RUN).
        group = section(envelopePage, 'ON EACH NOTE');
        row(group, 'envRetrigger', choice, { labels: ['FREE RUN', 'RETRIGGER'], stretch: true });
        group = section(envelopePage, 'ACCELERATE');
        detailRows.accelStart = row(group, 'accelStart', fader, { label: 'START RATE' });
        detailRows.accelEnd = row(group, 'accelEnd', fader, { label: 'END RATE' });
        detailRows.accelCurve = row(group, 'accelCurve', fader, { label: 'RATE CURVE' });

        const finishPage = document.querySelector('[data-page="2"]');
        group = section(finishPage, 'GRIT');
        detailRows.foldAmount = row(group, 'foldAmount', fader, { label: 'FOLD' });
        detailRows.foldPosition = row(group, 'foldPosition', choice, { labels: ['FILTER', 'PRE', 'POST'], stretch: true });
        row(group, 'cleanupMode', choice, { labels: ['CLEAN-UP OFF', 'ON'] });
        group = section(finishPage, 'FILTER');
        detailRows.filterMirror = row(group, 'filterMirror', fader, { label: 'MIRROR' });
        group = section(finishPage, 'COMPRESSION');
        const ottTime = parameter('ottTime');
        detailRows.ottTime = row(group, 'ottTime', fader, {
            label: 'OTT TIME',
            format: ottTime ? derivedDisplay(ottTime) : undefined,
        });

        buildLicense();

        const tabs = tabBar(['VOICE', 'ENVELOPE', 'FINISH', 'LICENSE'], {
            label: 'Settings pages',
            selected: 0,
            onSelect: showDetailsPage,
        });
        document.getElementById('details-tabs').appendChild(tabs.element);
        detailTabs = tabs;
        document.getElementById('details-toggle').addEventListener('click', toggleDetails);
        document.getElementById('details-close').addEventListener('click', closeDetails);
        setupPresetControls();
        // One Escape handler, innermost layer first, so a press never closes two layers at once.
        document.addEventListener('keydown', (event) => {
            if (event.key !== 'Escape') return;
            if (!document.getElementById('preset-dialog').hidden) closePresetDialog();
            else if (presetMenu) closePresetMenu();
            else if (drawingMenu) closeDrawingMenu();
            else if (detailsOpen) closeDetails();
        });
    }

    // MARK: Licensing

    function buildLicense() {
        const form = document.getElementById('license-form');
        const input = document.getElementById('license-input');
        form.addEventListener('submit', (event) => {
            event.preventDefault();
            activate(input.value);
        });
        // Inside a host, ⌘V usually goes to the host's Edit menu instead of this field, so the
        // plug-in reads the clipboard itself: from the button, or when ⌘V/Ctrl+V reaches the field
        // but nothing arrives.
        const pasteNatively = async (andActivate) => {
            const text = (await bdd.readClipboard()).trim();
            if (!text) {
                showLicenseMessage('The clipboard is empty. Copy the key from your receipt first.', true);
                return;
            }
            input.value = text;
            if (andActivate) activate(text);
        };
        document.getElementById('license-paste').addEventListener('click', () => pasteNatively(true));
        input.addEventListener('keydown', (event) => {
            if ((event.metaKey || event.ctrlKey) && (event.key === 'v' || event.key === 'V')) {
                const before = input.value;
                window.setTimeout(() => {
                    if (input.value === before) pasteNatively(false);
                }, 80);
            }
        });
        // A .bddlicense file dropped anywhere on the page is pasted and activated.
        const stage = document.getElementById('stage');
        const section = document.querySelector('.license-section');
        stage.addEventListener('dragover', (event) => {
            if (!event.dataTransfer || !Array.from(event.dataTransfer.types || []).includes('Files')) return;
            event.preventDefault();
            section.classList.add('dropping');
        });
        stage.addEventListener('dragleave', () => section.classList.remove('dropping'));
        stage.addEventListener('drop', (event) => {
            const file = event.dataTransfer && event.dataTransfer.files && event.dataTransfer.files[0];
            section.classList.remove('dropping');
            if (!file) return;
            event.preventDefault();
            file.text().then((text) => {
                showLicensePage();
                input.value = text.trim();
                activate(text);
            });
        });
        document.getElementById('license-badge').addEventListener('click', showLicensePage);
    }

    function showLicensePage() {
        openDetails();
        showDetailsPage(LICENSE_PAGE);
        if (detailTabs && detailTabs.select) detailTabs.select(LICENSE_PAGE);
    }

    async function activate(text) {
        if (licenseBusy || !licenseState) return;
        licenseBusy = true;
        showLicenseMessage('Activating…', false);
        document.getElementById('license-activate').disabled = true;
        try {
            await bdd.activate(licenseState.product, text);
            // The plug-in answers with licenseState and a status; see the receive handler.
        } catch (error) {
            licenseBusy = false;
            showLicenseMessage(error.message, true);
        } finally {
            document.getElementById('license-activate').disabled = false;
        }
    }

    function showLicenseMessage(message, error) {
        const element = document.getElementById('license-message');
        element.textContent = message || '';
        element.classList.toggle('error', Boolean(error));
    }

    function renderLicense() {
        if (!licenseState) return;
        const unlocked = licenseState.licensed || !licenseState.enforced;
        document.getElementById('license-badge').hidden = unlocked;
        document.querySelector('.license-section').classList.toggle('licensed', Boolean(licenseState.licensed));
        let summary;
        if (licenseState.licensed) {
            summary = 'Licensed to ' + (licenseState.licensee || licenseState.email || 'you') + '. Thank you!';
        } else if (!licenseState.enforced) {
            summary = 'Licensing isn’t switched on in this build, so YOI plays in full.';
        } else {
            summary = 'Demo: YOI goes silent for 3 seconds every minute until it’s activated. '
                + 'Paste the license key from your purchase receipt.';
        }
        document.getElementById('license-summary').textContent = summary;
        if (licenseState.licensed) document.getElementById('license-input').value = '';
    }

    function showDetailsPage(index) {
        document.querySelectorAll('.details-page').forEach((page, pageIndex) => {
            page.hidden = pageIndex !== index;
        });
    }

    function toggleDetails() {
        detailsOpen ? closeDetails() : openDetails();
    }

    function openDetails() {
        detailsOpen = true;
        const sheet = document.getElementById('details-sheet');
        sheet.classList.add('open');
        sheet.setAttribute('aria-hidden', 'false');
        document.getElementById('details-toggle').setAttribute('aria-expanded', 'true');
        document.getElementById('details-toggle').classList.add('selected');
    }

    function closeDetails() {
        detailsOpen = false;
        const sheet = document.getElementById('details-sheet');
        sheet.classList.remove('open');
        sheet.setAttribute('aria-hidden', 'true');
        document.getElementById('details-toggle').setAttribute('aria-expanded', 'false');
        document.getElementById('details-toggle').classList.remove('selected');
    }

    function setReason(rowInfo, dimmed, message) {
        if (!rowInfo) return;
        rowInfo.wrapper.classList.toggle('dimmed', dimmed);
        rowInfo.reason.textContent = dimmed ? message : '';
        rowInfo.wrapper.title = dimmed ? message : '';
    }

    function refresh() {
        if (!built) return;
        const free = Number(values.envTimeMode) >= 0.5;
        const sync = document.getElementById('time-sync');
        const freeTime = document.getElementById('time-free');
        if (sync) sync.hidden = free;
        if (freeTime) freeTime.hidden = !free;

        const accelerating = Number(values.envAccelerate ?? 0) >= 0.5 || isLegacyAccelerate();
        if (isLegacyAccelerate()) {
            for (const control of controls.get('envAccelerate') || []) control.set(1);
        }
        for (const key of ['accelStart', 'accelEnd', 'accelCurve']) {
            const rowInfo = detailRows[key];
            setReason(rowInfo, !accelerating, 'Used only when ACCEL is on.');
        }
        const foldOff = Number(values.foldAmount ?? 0) <= 0;
        setReason(detailRows.foldPosition, foldOff, 'Choose a fold position after increasing Fold.');
        const glideOff = Number(values.glideTime ?? 0) <= 0;
        setReason(detailRows.glideMode, glideOff, 'Glide mode is used only when Glide Time is above zero.');
        const powerOff = Number(values.macroPower ?? 0) <= 0;
        setReason(detailRows.ottTime, powerOff, 'OTT Time is used only when Power is above zero.');
        const lowPass = Number(values.filterMode ?? 0) < 0.5;
        setReason(detailRows.filterMirror, lowPass, 'Mirror is available in Band-pass mode.');
        const dsMode = Math.round(Number(values.dsMode ?? 1));
        const lock = document.getElementById('ds-lock');
        if (lock) {
            lock.title = dsMode !== 1 ? 'Lock applies only in S&H mode.' : '';
            lock.classList.toggle('bdd-dimmed', dsMode !== 1);
            lock.setAttribute('aria-label', dsMode !== 1 ? 'Lock applies only in S&H mode.' : 'Downsampler sync lock');
        }
        scheduleFilterGraph();

        // The two ends of the range the drawing sweeps, as the kernel works them out.
        const cutoff = parameter('cutoff');
        if (cutoff && filterState) {
            editor.setLabels(
                format(cutoff, filterState.cutoffTopHz).toUpperCase(),
                format(cutoff, filterState.cutoffBottomHz).toUpperCase(),
            );
        }
    }

    function build(descriptor) {
        parameters.clear();
        byAddress.clear();
        controls.clear();
        for (const descriptorItem of descriptor.parameters || []) {
            // DIR's controls cover the five directions, never the old sixth.
            const item = descriptorItem.identifier === 'envDirection'
                ? Object.assign({}, descriptorItem, { max: DIRECTIONS.length - 1, options: DIRECTIONS })
                : descriptorItem;
            parameters.set(item.identifier, item);
            byAddress.set(Number(item.id), item.identifier);
        }
        shapeNames = descriptor.shapes || [];

        buildMacros();
        buildDrawingControls();
        buildCharacter();
        buildLevel();
        buildCutoff();
        buildDetails();
        built = true;
        refresh();
    }

    function setupPresetControls() {
        document.getElementById('preset-select').addEventListener('click', (event) => {
            event.stopPropagation();
            presetMenu ? closePresetMenu() : openPresetMenu();
        });
        document.getElementById('preset-save').addEventListener('click', () => openPresetDialog('save'));
        document.getElementById('preset-delete').addEventListener('click', () => {
            if (!currentPreset || currentPreset.kind !== 'user') return;
            openPresetDialog('delete');
        });
        document.getElementById('preset-form').addEventListener('submit', (event) => {
            event.preventDefault();
            const mode = presetDialogMode;
            if (mode === 'delete' && currentPreset?.kind === 'user') {
                bdd.deletePreset(currentPreset.number);
                closePresetDialog();
                return;
            }
            if (mode === 'deleteDrawing' && pendingDrawingName) {
                bdd.deleteDrawing(pendingDrawingName);
                closePresetDialog();
                return;
            }
            const name = document.getElementById('preset-name-input').value.trim();
            if (mode === 'save' || mode === 'saveDrawing') {
                if (!name) {
                    showPresetStatus(mode === 'save' ? 'Enter a preset name.' : 'Enter a name for the drawing.', true);
                    document.getElementById('preset-name-input').focus();
                    return;
                }
                if (mode === 'save') bdd.savePreset(name);
                else bdd.saveDrawing(name);
            }
            closePresetDialog();
        });
        document.getElementById('preset-dialog-close').addEventListener('click', closePresetDialog);
        document.getElementById('preset-cancel').addEventListener('click', closePresetDialog);
        document.getElementById('preset-dialog').addEventListener('click', (event) => {
            if (event.target.id === 'preset-dialog') closePresetDialog();
        });
        document.addEventListener('click', (event) => {
            if (presetMenu && !event.target.closest('.preset-menu') && !event.target.closest('#preset-select')) closePresetMenu();
        });
        bdd.requestPresets();
    }

    /// The one naming and confirming dialog, for presets and for the user's drawings.
    /// `mode`: 'save' | 'delete' (presets), 'saveDrawing' | 'deleteDrawing' (with its `name`).
    function openPresetDialog(mode, name) {
        presetDialogMode = mode;
        pendingDrawingName = mode === 'deleteDrawing' ? name : null;
        const kicker = document.getElementById('preset-dialog-kicker');
        kicker.textContent = (mode === 'saveDrawing' || mode === 'deleteDrawing') ? 'YOUR DRAWING' : 'USER PRESET';
        const dialog = document.getElementById('preset-dialog');
        const title = document.getElementById('preset-dialog-title');
        const message = document.getElementById('preset-dialog-message');
        const label = document.getElementById('preset-name-field');
        const input = document.getElementById('preset-name-input');
        const confirm = document.getElementById('preset-confirm');
        if (mode === 'deleteDrawing') {
            title.textContent = 'Delete drawing?';
            message.textContent = `“${name || ''}” will be removed from your drawings. Presets that use it keep their own copy. This cannot be undone.`;
            label.hidden = true;
            confirm.textContent = 'DELETE DRAWING';
            confirm.classList.add('delete');
        } else if (mode === 'saveDrawing') {
            title.textContent = 'Save drawing';
            message.textContent = 'Name this drawing to use it in any patch. Only the drawing is saved, not the other settings.';
            label.hidden = false;
            input.value = '';
            confirm.textContent = 'SAVE DRAWING';
            confirm.classList.remove('delete');
        } else if (mode === 'delete') {
            title.textContent = 'Delete preset?';
            message.textContent = `“${currentPreset?.name || ''}” will be removed from your user presets. This cannot be undone.`;
            label.hidden = true;
            confirm.textContent = 'DELETE PRESET';
            confirm.classList.add('delete');
        } else {
            title.textContent = 'Save preset';
            message.textContent = 'Name this sound so you can find it again.';
            label.hidden = false;
            input.value = '';
            confirm.textContent = 'SAVE PRESET';
            confirm.classList.remove('delete');
        }
        dialog.hidden = false;
        if (mode === 'save' || mode === 'saveDrawing') window.setTimeout(() => input.focus(), 0);
    }

    function closePresetDialog() {
        document.getElementById('preset-dialog').hidden = true;
        document.getElementById('preset-confirm').classList.remove('delete');
        presetDialogMode = null;
        pendingDrawingName = null;
    }

    function renderPresetState() {
        const name = document.getElementById('preset-name');
        const remove = document.getElementById('preset-delete');
        if (currentPreset?.kind === 'user') {
            currentPreset = userPresets.find((preset) => preset.number === currentPreset.number) || currentPreset;
        }
        name.textContent = currentPreset?.name || 'UNSAVED PATCH';
        name.title = currentPreset?.name || 'Current settings are not a saved preset';
        remove.disabled = currentPreset?.kind !== 'user';
        const select = document.getElementById('preset-select');
        select.setAttribute('aria-label', currentPreset ? `Preset: ${currentPreset.name}` : 'Select preset');
        if (presetMenu) renderPresetMenuItems();
    }

    /// Places a pop-up list just under `anchor`, kept inside the (scaled) stage.
    function placeMenu(menu, anchor) {
        const stage = document.getElementById('stage');
        const scale = stage.getBoundingClientRect().width / (stage.offsetWidth || 1) || 1;
        const stageRect = stage.getBoundingClientRect();
        const anchorRect = anchor.getBoundingClientRect();
        const x = (anchorRect.left - stageRect.left) / scale;
        const y = (anchorRect.bottom - stageRect.top) / scale + 5;
        menu.style.left = `${Math.max(8, Math.min(x, stage.offsetWidth - menu.offsetWidth - 8))}px`;
        menu.style.top = `${Math.max(8, Math.min(y, stage.offsetHeight - menu.offsetHeight - 8))}px`;
    }

    function openPresetMenu() {
        if (drawingMenu) closeDrawingMenu();
        const select = document.getElementById('preset-select');
        const stage = document.getElementById('stage');
        presetMenu = document.createElement('div');
        presetMenu.className = 'preset-menu';
        presetMenu.setAttribute('role', 'listbox');
        presetMenu.setAttribute('aria-label', 'Available presets');
        stage.appendChild(presetMenu);
        select.setAttribute('aria-expanded', 'true');
        renderPresetMenuItems();
        placeMenu(presetMenu, select);
    }

    function renderPresetMenuItems() {
        if (!presetMenu) return;
        presetMenu.replaceChildren();
        const items = [...factoryPresets, ...userPresets];
        if (!items.length) {
            const empty = document.createElement('div');
            empty.className = 'preset-empty';
            empty.textContent = 'No presets yet. Use Save As to create one.';
            presetMenu.appendChild(empty);
            return;
        }
        for (const preset of items) {
            const button = document.createElement('button');
            button.type = 'button';
            button.className = 'preset-menu-item';
            button.setAttribute('role', 'option');
            button.setAttribute('aria-selected', String(currentPreset?.kind === preset.kind && currentPreset?.number === preset.number));
            const kind = document.createElement('span');
            kind.className = 'preset-kind';
            kind.textContent = preset.kind === 'factory' ? 'FACTORY' : 'USER';
            const title = document.createElement('span');
            title.textContent = preset.name;
            button.append(kind, title);
            button.addEventListener('click', () => {
                closePresetMenu();
                bdd.selectPreset(preset.number);
            });
            presetMenu.appendChild(button);
        }
    }

    function closePresetMenu() {
        presetMenu?.remove();
        presetMenu = null;
        document.getElementById('preset-select').setAttribute('aria-expanded', 'false');
    }

    function showPresetStatus(message, error) {
        const status = document.getElementById('preset-status');
        if (!status) return;
        status.textContent = message;
        status.classList.toggle('error', error);
        status.hidden = !message;
        if (message) window.setTimeout(() => { status.hidden = true; }, 3500);
    }

    /// DRAWINGS: the factory drawings, then the user's own (each deletable), then saving the
    /// current one. Loading any of them changes only the drawing, never the other settings.
    function buildDrawingsMenu() {
        const button = document.getElementById('drawings');
        button.setAttribute('aria-haspopup', 'listbox');
        button.setAttribute('aria-expanded', 'false');
        button.addEventListener('click', (event) => {
            event.stopPropagation();
            drawingMenu ? closeDrawingMenu() : openDrawingMenu();
        });
        document.addEventListener('click', (event) => {
            if (drawingMenu && !event.target.closest('.drawing-menu') && !event.target.closest('#drawings')) closeDrawingMenu();
        });
    }

    function openDrawingMenu() {
        if (presetMenu) closePresetMenu();
        const button = document.getElementById('drawings');
        drawingMenu = document.createElement('div');
        drawingMenu.className = 'preset-menu drawing-menu';
        drawingMenu.setAttribute('role', 'listbox');
        drawingMenu.setAttribute('aria-label', 'Drawings');
        document.getElementById('stage').appendChild(drawingMenu);
        button.setAttribute('aria-expanded', 'true');
        renderDrawingMenuItems();
        placeMenu(drawingMenu, button);
        bdd.requestDrawings();   // another instance may have saved one since
    }

    function renderDrawingMenuItems() {
        if (!drawingMenu) return;
        drawingMenu.replaceChildren();
        const addItem = (kind, name, onPick, onDelete) => {
            const row = document.createElement('div');
            row.className = 'drawing-menu-row';
            const button = document.createElement('button');
            button.type = 'button';
            button.className = 'preset-menu-item';
            button.setAttribute('role', 'option');
            const tag = document.createElement('span');
            tag.className = 'preset-kind';
            tag.textContent = kind;
            const title = document.createElement('span');
            title.textContent = name;
            button.append(tag, title);
            button.addEventListener('click', () => {
                closeDrawingMenu();
                onPick();
            });
            row.appendChild(button);
            if (onDelete) {
                const remove = document.createElement('button');
                remove.type = 'button';
                remove.className = 'drawing-delete';
                remove.textContent = '×';
                remove.title = 'Delete this drawing';
                remove.setAttribute('aria-label', `Delete drawing ${name}`);
                remove.addEventListener('click', (event) => {
                    event.stopPropagation();
                    closeDrawingMenu();
                    onDelete();
                });
                row.appendChild(remove);
            }
            drawingMenu.appendChild(row);
        };
        shapeNames.forEach((name, index) => addItem('FACTORY', String(name).toUpperCase(), () => bdd.loadShape(index)));
        if (userDrawings.length) {
            for (const name of userDrawings) {
                addItem('YOURS', name, () => bdd.loadDrawing(name), () => openPresetDialog('deleteDrawing', name));
            }
        } else {
            const empty = document.createElement('div');
            empty.className = 'preset-empty';
            empty.textContent = 'Drawings you save appear here.';
            drawingMenu.appendChild(empty);
        }
        const save = document.createElement('button');
        save.type = 'button';
        save.className = 'preset-menu-item drawing-save';
        save.textContent = 'SAVE CURRENT DRAWING…';
        save.addEventListener('click', () => {
            closeDrawingMenu();
            openPresetDialog('saveDrawing');
        });
        drawingMenu.appendChild(save);
    }

    function closeDrawingMenu() {
        drawingMenu?.remove();
        drawingMenu = null;
        document.getElementById('drawings').setAttribute('aria-expanded', 'false');
    }

    const editor = bdd.curveEditor(document.getElementById('curve'), Object.assign(bdd.curveStyles.modern(), {
        onEdit: (points) => bdd.setCurve(points),
        onHint: (text) => { document.getElementById('hint').textContent = text || ''; },
    }));

    function fit() {
        const stage = document.getElementById('stage');
        const scale = Math.min(window.innerWidth / STAGE_WIDTH, window.innerHeight / STAGE_HEIGHT);
        stage.style.transform = `scale(${scale})`;
        stage.style.left = `${Math.max(0, (window.innerWidth - STAGE_WIDTH * scale) / 2)}px`;
        stage.style.top = `${Math.max(0, (window.innerHeight - STAGE_HEIGHT * scale) / 2)}px`;
        editor.resize();
    }

    bdd.onReceive((incoming) => {
        if (incoming.descriptor && !built) build(incoming.descriptor);
        if (incoming.params) {
            for (const [id, value] of Object.entries(incoming.params)) {
                const identifier = byAddress.get(Number(id));
                if (!identifier) continue;
                values[identifier] = value;
                for (const control of controls.get(identifier) || []) control.set(value);
            }
            refresh();
        }
        if (incoming.curve) {
            editor.setPoints(incoming.curve.points || []);
            if (incoming.curve.table) editor.setTable(incoming.curve.table);
        }
        if (incoming.display) {
            filterState = incoming.display;
            editor.setPlayhead(incoming.display.position, incoming.display.value);
            if (levelControl && incoming.display.meterLeft !== undefined) {
                levelControl.meter(meterHeight(incoming.display.meterLeft), meterHeight(incoming.display.meterRight));
            }
            scheduleFilterGraph();
        }
        if (incoming.presetState) {
            userPresets = Array.isArray(incoming.presetState.userPresets) ? incoming.presetState.userPresets : [];
            factoryPresets = Array.isArray(incoming.presetState.factoryPresets) ? incoming.presetState.factoryPresets : [];
            currentPreset = incoming.presetState.currentPreset || null;
            renderPresetState();
        }
        if (incoming.drawingState) {
            userDrawings = Array.isArray(incoming.drawingState.drawings) ? incoming.drawingState.drawings : [];
            renderDrawingMenuItems();
        }
        if (incoming.licenseState) {
            licenseState = incoming.licenseState;
            renderLicense();
        }
        if (incoming.status && licenseBusy && incoming.licenseState) {
            // The answer to an activation: shown on the LICENSE page rather than as a toast.
            licenseBusy = false;
            showLicenseMessage(incoming.status.message || '', incoming.status.error === true);
        } else if (incoming.status) {
            showPresetStatus(incoming.status.message || '', incoming.status.error === true);
        }
    });

    buildDrawingsMenu();
    if (!bdd.connected) {
        const badge = document.getElementById('badge');
        badge.hidden = false;
        badge.textContent = window.bddStandIn ? 'BROWSER STAND-IN' : 'NOT CONNECTED';
    }
    window.addEventListener('resize', fit);
    fit();
    bdd.hello();
})();
