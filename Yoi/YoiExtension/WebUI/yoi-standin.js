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

    const SNAPSHOT = {"parameters":[{"default":0,"group":"output","id":0,"identifier":"outputLevel","log":false,"max":6,"min":-48,"name":"Output Level","unit":"db"},{"default":60,"group":"voice","id":10,"identifier":"glideTime","log":false,"max":2000,"min":0,"name":"Glide Time","unit":"ms"},{"default":0,"group":"voice","id":11,"identifier":"glideMode","log":false,"max":1,"min":0,"name":"Glide Mode","options":["Legato","Always"],"unit":"indexed"},{"default":2,"group":"voice","id":12,"identifier":"bendRange","log":false,"max":24,"min":0,"name":"Bend Range","unit":"semitones"},{"default":0,"group":"oscillators","id":20,"identifier":"oscShape","log":false,"max":100,"min":0,"name":"Osc Shape","unit":"percent"},{"default":75,"group":"oscillators","id":21,"identifier":"subLevel","log":false,"max":100,"min":0,"name":"Sub Level","unit":"percent"},{"default":0,"group":"oscillators","id":22,"identifier":"subShape","log":false,"max":100,"min":0,"name":"Sub Shape","unit":"percent"},{"default":0,"group":"oscillators","id":23,"identifier":"subOctave","log":false,"max":1,"min":0,"name":"Sub Octave","options":["-1 Oct","-2 Oct"],"unit":"indexed"},{"default":130,"group":"oscillators","id":24,"identifier":"subCrossover","log":true,"max":700,"min":50,"name":"Sub Crossover","unit":"hz"},{"default":0,"group":"filter","id":30,"identifier":"filterMode","log":false,"max":1,"min":0,"name":"Filter Mode","options":["LP","BP"],"unit":"indexed"},{"default":800,"group":"filter","id":31,"identifier":"cutoff","log":true,"max":2500,"min":20,"name":"Cutoff","unit":"hz"},{"default":29.999998092651367,"group":"filter","id":32,"identifier":"resonance","log":false,"max":100,"min":0,"name":"Resonance","unit":"percent"},{"default":3,"group":"amp","id":40,"identifier":"ampAttack","log":true,"max":5000,"min":0.10000000149011612,"name":"Attack","unit":"ms"},{"default":300,"group":"amp","id":41,"identifier":"ampDecay","log":true,"max":5000,"min":1,"name":"Decay","unit":"ms"},{"default":100,"group":"amp","id":42,"identifier":"ampSustain","log":false,"max":100,"min":0,"name":"Sustain","unit":"percent"},{"default":150,"group":"amp","id":43,"identifier":"ampRelease","log":true,"max":10000,"min":1,"name":"Release","unit":"ms"},{"default":3,"group":"envelope","id":50,"identifier":"envAmount","log":false,"max":8,"min":0,"name":"Env Amount","unit":"octaves"},{"default":0,"group":"envelope","id":51,"identifier":"envTimeMode","log":false,"max":1,"min":0,"name":"Env Time Mode","options":["Sync","Free"],"unit":"indexed"},{"default":6,"group":"envelope","id":52,"identifier":"envSyncLength","log":false,"max":23,"min":0,"name":"Env Sync","options":["1/64","1/48","1/32","1/24","1/16","1/12","1/8","1/6","3/16","1/4","5/16","1/3","3/8","1/2","3/4","1 bar","1.5 bars","2 bars","3 bars","4 bars","6 bars","8 bars","16 bars","32 bars"],"unit":"indexed"},{"default":500,"group":"envelope","id":53,"identifier":"envFreeTime","log":true,"max":30000,"min":10,"name":"Env Free Time","unit":"ms"},{"default":0,"group":"envelope","id":54,"identifier":"envDirection","log":false,"max":5,"min":0,"name":"Env Direction","options":["Forward","Backward","Pingpong","Sine","Random","Accelerate"],"unit":"indexed"},{"default":0,"group":"envelope","id":55,"identifier":"envRetrigger","log":false,"max":1,"min":0,"name":"Env Re-Trigger","unit":"boolean"},{"default":0.25,"group":"envelope","id":56,"identifier":"accelStart","log":true,"max":4,"min":0.10000000149011612,"name":"Accel Start","unit":"rate"},{"default":2,"group":"envelope","id":57,"identifier":"accelEnd","log":true,"max":4,"min":0.10000000149011612,"name":"Accel End","unit":"rate"},{"default":0,"group":"envelope","id":58,"identifier":"accelCurve","log":false,"max":1,"min":-1,"name":"Accel Curve","unit":"generic"},{"default":1,"group":"grit","id":70,"identifier":"dsMode","log":false,"max":2,"min":0,"name":"Downsampler","options":["Off","S&H","Downsample"],"unit":"indexed"},{"default":1400,"group":"grit","id":71,"identifier":"dsRate","log":true,"max":6000,"min":1300,"name":"S&H Rate","unit":"hz"},{"default":45,"group":"grit","id":72,"identifier":"dsAmount","log":false,"max":100,"min":0,"name":"Downsample Amount","unit":"percent"},{"default":0,"group":"grit","id":73,"identifier":"foldAmount","log":false,"max":100,"min":0,"name":"Fold","unit":"percent"},{"default":0,"group":"grit","id":74,"identifier":"foldPosition","log":false,"max":1,"min":0,"name":"Fold Position","options":["Pre-filter","Pre-downsample"],"unit":"indexed"},{"default":1,"group":"grit","id":75,"identifier":"cleanupMode","log":false,"max":1,"min":0,"name":"Clean-up","options":["Off","On"],"unit":"indexed"},{"default":5,"group":"grit","id":76,"identifier":"cleanupMultiple","log":true,"max":16,"min":1,"name":"Clean-up Multiple","unit":"ratio"}],"values":{"0":0,"10":60,"11":0,"12":2,"20":0,"21":75,"22":0,"23":0,"24":130,"30":0,"31":800,"32":29.999998092651367,"40":3,"41":300,"42":100,"43":150,"50":3,"51":0,"52":6,"53":500,"54":0,"55":0,"56":0.25,"57":2,"58":0,"70":1,"71":1400,"72":45,"73":0,"74":0,"75":1,"76":5},"shapes":[{"name":"Mock-up","points":[[0,0.33,0.6],[0.57,0.62,0],[0.76,0.62,0],[0.89,0.88,0],[1,0.33,0]]},{"name":"Wub","points":[[0,0.05,-0.5],[0.5,1,0.5],[1,0.05,0]]},{"name":"Rise","points":[[0,0,0.5],[1,1,0]]},{"name":"Pluck","points":[[0,1,-0.7],[1,0,0]]},{"name":"Gate","points":[[0,1,0],[0.5,1,0],[0.5,0,0],[1,0,0]]},{"name":"Talk","points":[[0,0.2,-0.4],[0.18,0.85,0.4],[0.33,0.35,-0.4],[0.5,0.95,0.4],[0.68,0.3,-0.4],[0.84,0.75,0.4],[1,0.2,0]]},{"name":"Steps","points":[[0,0.1,0],[0.25,0.1,0],[0.25,0.4,0],[0.5,0.4,0],[0.5,0.7,0],[0.75,0.7,0],[0.75,1,0],[1,1,0]]}]};

    const values = Object.assign({}, SNAPSHOT.values);
    let points = SNAPSHOT.shapes[0].points.map((point) => point.slice());
    let position = 0;

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

    window.bddStandIn = {
        handle(message) {
            switch (message.type) {
                case 'hello':
                    send({
                        descriptor: { parameters: SNAPSHOT.parameters, shapes: SNAPSHOT.shapes.map((shape) => shape.name) },
                        params: values,
                        curve: curve(),
                    });
                    setInterval(() => {
                        position = (position + 1 / 30 / 1.2) % 1;   // a 1.2 s pass
                        const table = approximateTable(256);
                        window.bdd.receive({ display: { position, value: table[Math.round(position * 255)] } });
                    }, 1000 / 30);
                    break;
                case 'edit':
                    values[String(message.id)] = message.value;
                    break;
                case 'setCurve':
                    points = message.points.map((point) => point.slice());
                    send({ curve: curve() });
                    break;
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
