// yoi-standin.js
// A stand-in for the plug-in, so the editor can be opened straight in a browser (serve this
// folder, e.g. `python3 -m http.server`) and worked on without rebuilding or opening a host.
// It does nothing inside the plug-in, where the real bridge is connected.
//
// SNAPSHOT is YOI's parameter list, defaults and factory drawings, generated from the built audio
// unit and YoiFactoryShapes.hpp. Regenerate it when those change (see YOI_WORKSPACE/README.md).
//
// One honest difference: the real curve is worked out by the plug-in's C++. Here bent segments
// are only approximated (with a power curve, not the plug-in's maths), so bends look roughly
// right in a browser and exactly right inside YOI.

(function () {
    'use strict';
    if (window.bdd && window.bdd.connected) {
        return;
    }

    const SNAPSHOT = {"parameters":[{"default":0,"group":"macros","id":90,"identifier":"macroVoice","log":false,"max":100,"min":0,"name":"Voice","unit":"percent"},{"default":100,"group":"macros","id":91,"identifier":"macroThroat","log":false,"max":100,"min":0,"name":"Throat","unit":"percent"},{"default":0,"group":"macros","id":92,"identifier":"macroPower","log":false,"max":100,"min":0,"name":"Power","unit":"percent"},{"default":0,"group":"macros","id":93,"identifier":"macroControl","log":false,"max":100,"min":0,"name":"Control","unit":"percent"},{"default":0,"group":"macros","id":94,"identifier":"macroWidth","log":false,"max":100,"min":0,"name":"Width","unit":"percent"},{"default":0,"group":"output","id":0,"identifier":"outputLevel","log":false,"max":6,"min":-48,"name":"Output Level","unit":"db"},{"default":60,"group":"voice","id":10,"identifier":"glideTime","log":false,"max":2000,"min":0,"name":"Glide Time","unit":"ms"},{"default":0,"group":"voice","id":11,"identifier":"glideMode","log":false,"max":1,"min":0,"name":"Glide Mode","options":["Legato","Always"],"unit":"indexed"},{"default":2,"group":"voice","id":12,"identifier":"bendRange","log":false,"max":24,"min":0,"name":"Bend Range","unit":"semitones"},{"default":0,"group":"oscillators","id":23,"identifier":"subOctave","log":false,"max":1,"min":0,"name":"Sub Octave","options":["-1 Oct","-2 Oct"],"unit":"indexed"},{"default":130,"group":"oscillators","id":24,"identifier":"subCrossover","log":true,"max":700,"min":50,"name":"Sub Crossover","unit":"hz"},{"default":0,"group":"oscillators","id":25,"identifier":"subFollow","log":false,"max":100,"min":0,"name":"Sub Follow","unit":"percent"},{"default":0,"group":"filter","id":30,"identifier":"filterMode","log":false,"max":1,"min":0,"name":"Filter Mode","options":["LP","BP"],"unit":"indexed"},{"default":800,"group":"filter","id":31,"identifier":"cutoff","log":true,"max":2500,"min":20,"name":"Cutoff","unit":"hz"},{"default":29.999998092651367,"group":"filter","id":32,"identifier":"resonance","log":false,"max":100,"min":0,"name":"Resonance","unit":"percent"},{"default":0,"group":"filter","id":33,"identifier":"filterMirror","log":false,"max":100,"min":0,"name":"Mirror","unit":"percent"},{"default":3,"group":"amp","id":40,"identifier":"ampAttack","log":true,"max":5000,"min":0.10000000149011612,"name":"Attack","unit":"ms"},{"default":300,"group":"amp","id":41,"identifier":"ampDecay","log":true,"max":5000,"min":1,"name":"Decay","unit":"ms"},{"default":100,"group":"amp","id":42,"identifier":"ampSustain","log":false,"max":100,"min":0,"name":"Sustain","unit":"percent"},{"default":150,"group":"amp","id":43,"identifier":"ampRelease","log":true,"max":10000,"min":1,"name":"Release","unit":"ms"},{"default":3,"group":"envelope","id":50,"identifier":"envAmount","log":false,"max":8,"min":0,"name":"Env Amount","unit":"octaves"},{"default":0,"group":"envelope","id":51,"identifier":"envTimeMode","log":false,"max":1,"min":0,"name":"Env Time Mode","options":["Sync","Free"],"unit":"indexed"},{"default":16,"group":"envelope","id":52,"identifier":"envSyncLength","log":false,"max":23,"min":0,"name":"Env Sync","options":["1/64","1/48","1/32","1/24","1/16","1/12","1/8","1/6","3/16","1/4","5/16","1/3","3/8","1/2","3/4","1 bar","1.5 bars","2 bars","3 bars","4 bars","6 bars","8 bars","16 bars","32 bars"],"unit":"indexed"},{"default":500,"group":"envelope","id":53,"identifier":"envFreeTime","log":true,"max":30000,"min":10,"name":"Env Free Time","unit":"ms"},{"default":0,"group":"envelope","id":54,"identifier":"envDirection","log":false,"max":5,"min":0,"name":"Env Direction","options":["Forward","Backward","Pingpong","Sine","Random","Accelerate"],"unit":"indexed"},{"default":0,"group":"envelope","id":59,"identifier":"envAccelerate","log":false,"max":1,"min":0,"name":"Env Accelerate","options":["Off","On"],"unit":"indexed"},{"default":0,"group":"envelope","id":55,"identifier":"envRetrigger","log":false,"max":1,"min":0,"name":"Env Re-Trigger","unit":"boolean"},{"default":0.25,"group":"envelope","id":56,"identifier":"accelStart","log":true,"max":4,"min":0.10000000149011612,"name":"Accel Start","unit":"rate"},{"default":2,"group":"envelope","id":57,"identifier":"accelEnd","log":true,"max":4,"min":0.10000000149011612,"name":"Accel End","unit":"rate"},{"default":0,"group":"envelope","id":58,"identifier":"accelCurve","log":false,"max":1,"min":-1,"name":"Accel Curve","unit":"generic"},{"default":1,"group":"grit","id":70,"identifier":"dsMode","log":false,"max":2,"min":0,"name":"Downsampler","options":["Off","S&H","Downsample"],"unit":"indexed"},{"default":0,"group":"grit","id":73,"identifier":"foldAmount","log":false,"max":5,"min":0,"name":"Fold","unit":"percent"},{"default":2,"group":"grit","id":74,"identifier":"foldPosition","log":false,"max":2,"min":0,"name":"Fold Position","options":["Pre-filter","Pre-downsample","Post-downsample"],"unit":"indexed"},{"default":1,"group":"grit","id":75,"identifier":"cleanupMode","log":false,"max":1,"min":0,"name":"Clean-up","options":["Off","On"],"unit":"indexed"},{"default":0,"group":"grit","id":77,"identifier":"dsLock","log":false,"max":1,"min":0,"name":"S&H Lock","options":["Free","Lock"],"unit":"indexed"},{"default":50,"group":"finish","id":83,"identifier":"ottTime","log":false,"max":100,"min":0,"name":"OTT Time","unit":"percent","valueDisplays":["25 ms","26 ms","26 ms","27 ms","28 ms","29 ms","30 ms","30 ms","31 ms","32 ms","33 ms","34 ms","35 ms","36 ms","37 ms","38 ms","39 ms","40 ms","41 ms","42 ms","44 ms","45 ms","46 ms","47 ms","49 ms","50 ms","51 ms","53 ms","54 ms","56 ms","57 ms","59 ms","61 ms","62 ms","64 ms","66 ms","68 ms","70 ms","72 ms","74 ms","76 ms","78 ms","80 ms","82 ms","85 ms","87 ms","90 ms","92 ms","95 ms","97 ms","100 ms","103 ms","106 ms","109 ms","112 ms","115 ms","118 ms","121 ms","125 ms","128 ms","132 ms","136 ms","139 ms","143 ms","147 ms","152 ms","156 ms","160 ms","165 ms","169 ms","174 ms","179 ms","184 ms","189 ms","195 ms","200 ms","206 ms","211 ms","217 ms","223 ms","230 ms","236 ms","243 ms","250 ms","257 ms","264 ms","271 ms","279 ms","287 ms","295 ms","303 ms","312 ms","320 ms","329 ms","339 ms","348 ms","358 ms","368 ms","378 ms","389 ms","400 ms"]}],"values":{"0":0,"10":60,"11":0,"12":2,"23":0,"24":130,"25":0,"30":0,"31":800,"32":29.999998092651367,"33":0,"40":3,"41":300,"42":100,"43":150,"50":3,"51":0,"52":16,"53":500,"54":0,"55":0,"56":0.25,"57":2,"58":0,"59":0,"70":1,"73":0,"74":2,"75":1,"77":0,"83":50,"90":0,"91":100,"92":0,"93":0,"94":0},"shapes":[{"name":"Mock-up","points":[[0,0.33,0.6],[0.57,0.62,0],[0.76,0.62,0],[0.89,0.88,0],[1,0.33,0]]},{"name":"Wub","points":[[0,0.05,-0.5],[0.5,1,0.5],[1,0.05,0]]},{"name":"Rise","points":[[0,0,0.5],[1,1,0]]},{"name":"Pluck","points":[[0,1,-0.7],[1,0,0]]},{"name":"Gate","points":[[0,1,0],[0.5,1,0],[0.5,0,0],[1,0,0]]},{"name":"Talk","points":[[0,0.2,-0.4],[0.18,0.85,0.4],[0.33,0.35,-0.4],[0.5,0.95,0.4],[0.68,0.3,-0.4],[0.84,0.75,0.4],[1,0.2,0]]},{"name":"Steps","points":[[0,0.1,0],[0.25,0.1,0],[0.25,0.4,0],[0.5,0.4,0],[0.5,0.7,0],[0.75,0.7,0],[0.75,1,0],[1,1,0]]},{"name":"Init","points":[[0,1,0],[1,1,0]]}]};

    const values = Object.assign({}, SNAPSHOT.values);
    let points = SNAPSHOT.shapes[0].points.map((point) => point.slice());
    let position = 0;
    // The user's drawings, kept in memory for as long as the page is open.
    const userDrawings = [];
    const drawingState = () => ({ drawings: userDrawings.map((drawing) => drawing.name) });
    const findDrawing = (name) => userDrawings.findIndex((drawing) => drawing.name.toLowerCase() === String(name).toLowerCase());

    /** Roughly the plug-in's bend: positive starts slowly, negative starts fast. */
    function approximateBend(t, bend) {
        return Math.pow(t, Math.pow(2, 2.5 * bend));
    }

    function approximateTable(size) {
        const table = [];
        let segment = 0;
        for (let i = 0; i < size; i++) {
            const x = i / (size - 1);
            while (segment + 2 < points.length && points[segment + 1][0] <= x) {
                segment++;
            }
            const [x0, y0, bend] = points[segment];
            const [x1, y1] = points[Math.min(segment + 1, points.length - 1)];
            const t = x1 > x0 ? Math.min(1, Math.max(0, (x - x0) / (x1 - x0))) : 1;
            table.push(y0 + (y1 - y0) * approximateBend(t, bend || 0));
        }
        return table;
    }

    function curve() {
        return { points, table: approximateTable(256) };
    }

    function send(state) {
        // The plug-in answers asynchronously; so does the stand-in.
        setTimeout(() => window.bdd.receive(state), 0);
    }

    /// Stands in for the filter values the kernel publishes. This is the one place allowed to
    /// approximate the kernel's maths, because standing in for the kernel is the whole job: the
    /// page must not do this itself. Close enough for the browser; the plug-in sends the real thing.
    function filterDisplay(drawing) {
        const cutoff = Number(values['31'] ?? 800);
        const amount = Number(values['50'] ?? 3);
        const resonance = Math.max(0, Math.min(100, Number(values['32'] ?? 30))) / 100;
        const q = 0.7071 * Math.pow(20 / 0.7071, 0.07 + (0.85 - 0.07) * resonance);
        return {
            cutoffHz: cutoff * Math.pow(2, amount * (drawing - 1)),
            filterQ: q,
            cutoffTopHz: cutoff,
            cutoffBottomHz: cutoff * Math.pow(2, -amount),
        };
    }

    // Licensing as an enforced, unlicensed build sees it, so the DEMO button and LICENSE page can be
    // worked on. Any pasted "BDD1." line counts as licensed here; only the plug-in really checks.
    let licenseState = { product: 'YOI', enforced: true, licensed: false, licensee: '', email: '' };

    window.bddStandIn = {
        handle(message) {
            switch (message.type) {
                case 'hello':
                    send({
                        descriptor: { parameters: SNAPSHOT.parameters, shapes: SNAPSHOT.shapes.map((shape) => shape.name) },
                        params: values,
                        curve: curve(),
                        drawingState: drawingState(),
                        licenseState,
                        display: Object.assign({ position: 0, value: 1 }, filterDisplay(1)),
                    });
                    setInterval(() => {
                        position = (position + 1 / 30 / 1.2) % 1;   // a 1.2 s pass
                        const table = approximateTable(256);
                        const value = table[Math.round(position * 255)];
                        // Pretend output levels that follow the drawing, a little wider on the right.
                        const meters = { meterLeft: 0.2 + 0.25 * value, meterRight: 0.18 + 0.3 * value };
                        window.bdd.receive({ display: Object.assign({ position, value }, filterDisplay(value), meters) });
                    }, 1000 / 30);
                    break;
                case 'edit':
                    values[String(message.id)] = message.value;
                    break;
                case 'setCurve':
                    points = message.points.map((point) => point.slice());
                    send({ curve: curve() });
                    break;
                case 'drawingList':
                    send({ drawingState: drawingState() });
                    break;
                case 'drawingSave': {
                    const name = String(message.name || '').trim();
                    if (!name || findDrawing(name) >= 0) {
                        send({ status: { message: name ? 'A drawing with that name already exists.' : 'Enter a name for the drawing.', error: true } });
                        break;
                    }
                    userDrawings.push({ name, points: points.map((point) => point.slice()) });
                    send({ drawingState: drawingState(), status: { message: `Saved drawing “${name}”.`, error: false } });
                    break;
                }
                case 'drawingLoad': {
                    const index = findDrawing(message.name);
                    if (index >= 0) {
                        points = userDrawings[index].points.map((point) => point.slice());
                        send({ curve: curve() });
                    }
                    break;
                }
                case 'drawingDelete': {
                    const index = findDrawing(message.name);
                    if (index >= 0) userDrawings.splice(index, 1);
                    send({ drawingState: drawingState(), status: { message: 'Drawing deleted.', error: false } });
                    break;
                }
                case 'licenseInstall': {
                    const valid = String(message.license || '').startsWith('BDD1.');
                    if (valid) licenseState = Object.assign({}, licenseState, { licensed: true, licensee: 'Stand-in Buyer' });
                    send({ licenseState, status: { message: valid ? 'Licensed.' : 'That isn’t a Bass Daddy Devices license or key.', error: !valid } });
                    break;
                }
                case 'loadShape': {
                    const shape = SNAPSHOT.shapes[message.index];
                    if (shape) {
                        points = shape.points.map((point) => point.slice());
                        send({ curve: curve() });
                    }
                    break;
                }
                default:
                    break;
            }
        },
    };
})();
