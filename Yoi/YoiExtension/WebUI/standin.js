// standin.js
// A stand-in for the plug-in, so the editor can be opened straight in a browser (serve this
// folder, e.g. `python3 -m http.server`) and worked on without rebuilding or opening a host.
// It does nothing inside the plug-in, where the real bridge is connected.
//
// SNAPSHOT is the plug-in's parameter list and default drawing, generated from the built audio
// unit. Regenerate it when parameters change (see YOI_WORKSPACE/README.md).
//
// One honest difference: the real curve is worked out by the plug-in's C++, so here the drawing
// is joined with straight lines and bends are ignored. Inside YOI, what's drawn is always the
// plug-in's own curve.

(function () {
    'use strict';
    if (window.bdd && window.bdd.connected) {
        return;
    }

    const SNAPSHOT = {"parameters":[{"group":"output","id":0,"identifier":"outputLevel","log":false,"max":6,"min":-48,"name":"Output Level","unit":"db"},{"group":"voice","id":10,"identifier":"glideTime","log":false,"max":2000,"min":0,"name":"Glide Time","unit":"ms"},{"group":"voice","id":11,"identifier":"glideMode","log":false,"max":1,"min":0,"name":"Glide Mode","options":["Legato","Always"],"unit":"indexed"},{"group":"voice","id":12,"identifier":"bendRange","log":false,"max":24,"min":0,"name":"Bend Range","unit":"semitones"},{"group":"oscillators","id":20,"identifier":"oscShape","log":false,"max":100,"min":0,"name":"Osc Shape","unit":"percent"},{"group":"oscillators","id":21,"identifier":"subLevel","log":false,"max":100,"min":0,"name":"Sub Level","unit":"percent"},{"group":"oscillators","id":22,"identifier":"subShape","log":false,"max":100,"min":0,"name":"Sub Shape","unit":"percent"},{"group":"oscillators","id":23,"identifier":"subOctave","log":false,"max":1,"min":0,"name":"Sub Octave","options":["-1 Oct","-2 Oct"],"unit":"indexed"},{"group":"filter","id":30,"identifier":"filterMode","log":false,"max":1,"min":0,"name":"Filter Mode","options":["LP","BP"],"unit":"indexed"},{"group":"filter","id":31,"identifier":"cutoff","log":true,"max":2500,"min":20,"name":"Cutoff","unit":"hz"},{"group":"filter","id":32,"identifier":"resonance","log":false,"max":100,"min":0,"name":"Resonance","unit":"percent"},{"group":"amp","id":40,"identifier":"ampAttack","log":true,"max":5000,"min":0.10000000149011612,"name":"Attack","unit":"ms"},{"group":"amp","id":41,"identifier":"ampDecay","log":true,"max":5000,"min":1,"name":"Decay","unit":"ms"},{"group":"amp","id":42,"identifier":"ampSustain","log":false,"max":100,"min":0,"name":"Sustain","unit":"percent"},{"group":"amp","id":43,"identifier":"ampRelease","log":true,"max":10000,"min":1,"name":"Release","unit":"ms"},{"group":"envelope","id":50,"identifier":"envAmount","log":false,"max":8,"min":0,"name":"Env Amount","unit":"octaves"},{"group":"envelope","id":51,"identifier":"envTimeMode","log":false,"max":1,"min":0,"name":"Env Time Mode","options":["Sync","Free"],"unit":"indexed"},{"group":"envelope","id":52,"identifier":"envSyncLength","log":false,"max":23,"min":0,"name":"Env Sync","options":["1/64","1/48","1/32","1/24","1/16","1/12","1/8","1/6","3/16","1/4","5/16","1/3","3/8","1/2","3/4","1 bar","1.5 bars","2 bars","3 bars","4 bars","6 bars","8 bars","16 bars","32 bars"],"unit":"indexed"},{"group":"envelope","id":53,"identifier":"envFreeTime","log":true,"max":30000,"min":10,"name":"Env Free Time","unit":"ms"},{"group":"envelope","id":54,"identifier":"envDirection","log":false,"max":5,"min":0,"name":"Env Direction","options":["Forward","Backward","Pingpong","Sine","Random","Accelerate"],"unit":"indexed"},{"group":"envelope","id":55,"identifier":"envRetrigger","log":false,"max":1,"min":0,"name":"Env Re-Trigger","unit":"boolean"},{"group":"envelope","id":56,"identifier":"accelStart","log":true,"max":4,"min":0.10000000149011612,"name":"Accel Start","unit":"rate"},{"group":"envelope","id":57,"identifier":"accelEnd","log":true,"max":4,"min":0.10000000149011612,"name":"Accel End","unit":"rate"},{"group":"envelope","id":58,"identifier":"accelCurve","log":false,"max":1,"min":-1,"name":"Accel Curve","unit":"generic"},{"group":"grit","id":70,"identifier":"dsMode","log":false,"max":2,"min":0,"name":"Downsampler","options":["Off","S&H","Downsample"],"unit":"indexed"},{"group":"grit","id":71,"identifier":"dsRate","log":true,"max":6000,"min":1300,"name":"S&H Rate","unit":"hz"},{"group":"grit","id":72,"identifier":"dsAmount","log":false,"max":100,"min":0,"name":"Downsample Amount","unit":"percent"},{"group":"grit","id":73,"identifier":"foldAmount","log":false,"max":100,"min":0,"name":"Fold","unit":"percent"},{"group":"grit","id":74,"identifier":"foldPosition","log":false,"max":1,"min":0,"name":"Fold Position","options":["Pre-filter","Pre-downsample"],"unit":"indexed"},{"group":"grit","id":75,"identifier":"cleanupMode","log":false,"max":1,"min":0,"name":"Clean-up","options":["Off","On"],"unit":"indexed"},{"group":"grit","id":76,"identifier":"cleanupMultiple","log":true,"max":16,"min":1,"name":"Clean-up Multiple","unit":"ratio"}],"points":[[0,0.33000001311302185,0.6000000238418579],[0.5699999928474426,0.6200000047683716,0],[0.7599999904632568,0.6200000047683716,0],[0.8899999856948853,0.8799999952316284,0],[1,0.33000001311302185,0]],"values":{"0":0,"10":60,"11":0,"12":2,"20":0,"21":50,"22":0,"23":0,"30":0,"31":800,"32":29.999998092651367,"40":3,"41":300,"42":100,"43":150,"50":3,"51":0,"52":6,"53":500,"54":0,"55":0,"56":0.25,"57":2,"58":0,"70":1,"71":1400,"72":45,"73":0,"74":0,"75":1,"76":5}};
    const SHAPES = ['Mock-up', 'Wub', 'Rise', 'Pluck', 'Gate', 'Talk', 'Steps'];
    const values = Object.assign({}, SNAPSHOT.values);
    let points = SNAPSHOT.points.map((point) => point.slice());
    let position = 0;

    function straightTable(size) {
        const table = [];
        for (let i = 0; i < size; i++) {
            const x = i / (size - 1);
            let segment = 0;
            while (segment + 2 < points.length && points[segment + 1][0] <= x) {
                segment++;
            }
            const [x0, y0] = points[segment];
            const [x1, y1] = points[Math.min(segment + 1, points.length - 1)];
            const t = x1 > x0 ? Math.min(1, Math.max(0, (x - x0) / (x1 - x0))) : 1;
            table.push(y0 + (y1 - y0) * t);
        }
        return table;
    }

    function curve() {
        return { points, table: straightTable(256) };
    }

    function readAt(x) {
        const table = straightTable(256);
        return table[Math.round(x * 255)];
    }

    window.bddStandIn = {
        handle(message) {
            switch (message.type) {
                case 'hello':
                    window.bdd.receive({
                        descriptor: { parameters: SNAPSHOT.parameters, shapes: SHAPES },
                        params: values,
                        curve: curve(),
                    });
                    setInterval(() => {
                        position = (position + 1 / 30 / 0.5) % 1;   // a 500 ms pass
                        window.bdd.receive({ display: { position, value: readAt(position) } });
                    }, 1000 / 30);
                    break;
                case 'edit':
                    values[String(message.id)] = message.value;
                    break;
                case 'setCurve':
                    points = message.points.map((point) => point.slice());
                    window.bdd.receive({ curve: curve() });
                    break;
                case 'loadShape':
                    points = [[0, 0.5, 0], [1, 0.5, 0]];
                    window.bdd.receive({ curve: curve() });
                    break;
                default:
                    break;
            }
        },
    };
})();
