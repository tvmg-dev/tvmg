const char status_html[] = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>HP Monitor - Live</title>
    <style>
        :root {
            --bg-color: #1a1a1a;
            --card-bg: #2d2d2d;
            --text-main: #e0e0e0;
            --accent: #00adb5;
        }
        body { font-family: sans-serif; background: var(--bg-color); color: var(--text-main); margin: 20px; }
        .container { max-width: 500px; margin: auto; }
        .header { display: flex; justify-content: space-between; align-items: center; border-bottom: 2px solid var(--accent); padding-bottom: 10px; margin-bottom: 20px; }
        .card { background: var(--card-bg); padding: 15px; border-radius: 8px; margin-bottom: 15px; box-shadow: 0 4px 6px rgba(0,0,0,0.3); }
        .row { display: flex; justify-content: space-between; padding: 8px 0; border-bottom: 1px solid #3d3d3d; }
        .row:last-child { border-bottom: none; }
        .label { color: #aaa; font-size: 0.9em; }
        .value { font-family: 'Courier New', monospace; font-weight: bold; font-size: 1.1em; color: var(--accent); }
        #status { font-size: 0.7em; padding: 3px 8px; border-radius: 4px; text-transform: uppercase; }
        .online { background: #1b5e20; }
        .offline { background: #b71c1c; }
        .status-dot { height: 10px; width: 10px; border-radius: 50%; display: inline-block; margin-right: 8px; vertical-align: middle; }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h3>THERMAL LIVE</h3>
            <span id="status" class="offline">Offline</span>
        </div>

        <div class="card">
            <div class="row">
                <span class="label">Updated</span>
                <span class="value" id="time">--:--:--</span>
            </div>
        </div>

        <div class="card">
            <div class="row"><span class="label">HP Flow</span><span class="value" id="hp-flow">--.-</span></div>
            <div class="row"><span class="label">HP Return</span><span class="value" id="hp-return">--.-</span></div>
        </div>

        <div class="card">
            <div class="row">
                <span class="label">Silent Mode</span>
                <span><span id="silent-dot" class="status-dot" style="background:gray"></span><span class="value" id="silent-on">---</span></span>
            </div>
            <div class="row"><span class="label">Inlet</span><span class="value" id="inlet">--.-</span></div>
            <div class="row"><span class="label">Outlet</span><span class="value" id="outlet">--.-</span></div>
        </div>
    </div>

    <script>
        const source = new EventSource('/telemetry');
        const status = document.getElementById('status');

        source.onopen = () => { status.innerText = "Online"; status.className = "online"; };
        source.onerror = () => { status.innerText = "Offline"; status.className = "offline"; };

        source.onmessage = (e) => {
            try {
                const d = JSON.parse(e.data);

                document.getElementById('time').innerText = d.time;
                document.getElementById('hp-flow').innerText = d.temperatures["HP Flow"] ?? "--";
                document.getElementById('hp-return').innerText = d.temperatures["HP Return"] ?? "--";
                document.getElementById('inlet').innerText = d.LG.inlet ?? "--";
                document.getElementById('outlet').innerText = d.LG.outlet ?? "--";

                const silentOn = d.LG["silent-on"];
                document.getElementById('silent-on').innerText = silentOn ? "ON" : "OFF";
                document.getElementById('silent-dot').style.backgroundColor = silentOn ? "#4caf50" : "#f44336";

            } catch (err) { console.error("Parse error", err); }
        };
    </script>
</body>
</html>
)rawliteral";

