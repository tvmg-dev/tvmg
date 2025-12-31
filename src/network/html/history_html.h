const char history_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html lang="en">
<head>
    <title>ThermaV Event Log</title>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    %STYLE%
</head>
<body>
    <div class="container">
        <div class="header-nav">
            <a href="/manager" class="nav-btn">&larr; Manager</a>
            <h2>ThermaV Event Log</h2>
            <div style="width:85px"></div>
        </div>
        <fieldset>
            <legend>System Activity (Latest First)</legend>

        <div class="log-row" style="font-weight:bold; color:#666; font-size:10px; border-bottom:2px solid #ccc; text-transform:uppercase;">
            <div>Time</div>
            <div style="text-align:center;">Mode</div>
            <div class="cp-col" style="text-align:center;">CP</div>
            <div>Inlet/Outlet</div>
            <div>Target</div>
            <div>DHW/Target</div>
            <div class="outdoor-col">Outdoor</div>
            <div style="text-align:center;">Flags</div>
            <div style="text-align:center;">Err</div>
        </div>

            <div id="log-target"><div class="loader">Accessing log files...</div></div>
        </fieldset>
        <div id="last-upd">Initializing...</div>
    </div>

    <script>
        async function loadLogs() {
            const target = document.getElementById('log-target');
            const upd = document.getElementById('last-upd');
            try {
                const [y, t] = await Promise.all([
                    fetch('/lgstatus2.html').then(r => r.ok ? r.text() : ""),
                    fetch('/lgstatus.html').then(r => r.ok ? r.text() : "")
                ]);

                const doc = new DOMParser().parseFromString(`<div>${y}${t}</div>`, 'text/html');
                const rows = doc.querySelectorAll('tr');
                let fragment = document.createDocumentFragment();

                rows.forEach(row => {
                    const td = row.querySelectorAll('td');
                    if (td.length === 9) {
                        const r = document.createElement('div');
                        r.className = 'log-row';

                        const rawMode = td[1].innerText.trim().toUpperCase();
                        const rawCP   = td[2].innerText.trim().toUpperCase();
                        const rawErr  = td[8].innerText.trim();

                        // Logic: Explicit checks for fixed strings, default everything else to AI
                        let modeClass = "mode-ai";
                        if (rawMode === "OFF") {
                            modeClass = "mode-off";
                        } else if (rawMode === "HEAT") {
                            modeClass = "mode-heat";
                        } else if (rawMode === "DHW") {
                            modeClass = "mode-dhw";
                        } else if (rawMode === "ERROR") {
                            modeClass = "mode-error";
                        }

                        const cpClass = (rawCP === 'ON') ? 'cp-on' : '';

                        r.innerHTML = `
                            <div class="time">${td[0].innerText}</div>
                            <div class="mode-cell ${modeClass}">${td[1].innerText}</div>
                            <div class="cp-col ${cpClass}" style="text-align:center;">${td[2].innerText}</div>
                            <div><span class="val-unit">${td[3].innerText}</span></div>
                            <div><span class="val-unit">${td[4].innerText}</span></div>
                            <div><span class="val-unit">${td[5].innerText}</span></div>
                            <div class="outdoor-col"><span class="val-unit">${td[6].innerText}</span></div>
                            <div class="flags" style="text-align:center;">${td[7].innerHTML}</div>
                            <div class="err-cell ${rawErr !== "0" ? 'err-active' : 'err-none'}">${rawErr}</div>
                        `;
                        fragment.appendChild(r);
                    }
                });

                if(fragment.children.length > 0) {
                    target.innerHTML = '';
                    target.appendChild(fragment);
                } else {
                    target.innerHTML = '<div class="loader">No log entries found.</div>';
                }
                upd.innerText = "Last Synchronized: " + new Date().toLocaleTimeString();
            } catch (e) {
                target.innerHTML = '<div class="loader">Log retrieval failed.</div>';
            }
        }
        loadLogs();
        setInterval(loadLogs, 30000);
    </script>
</body>
</html>
)rawliteral";
