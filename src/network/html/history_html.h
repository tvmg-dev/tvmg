const char history_html[] = R"raw(
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
            <div style="width:85px"></div> </div>

        <fieldset>
            <legend>System Activity (Latest First)</legend>
            <div id="log-target">
                <div class="loader">Accessing log files...</div>
            </div>
        </fieldset>

        <div id="last-upd">Initializing...</div>
    </div>

    <script>
        async function loadLogs() {
            const target = document.getElementById('log-target');
            const upd = document.getElementById('last-upd');
            try {
                // Fetching the files based on your SPIFFS structure
                const [y, t] = await Promise.all([
                    fetch('/lgstatusold.html').then(r => r.ok ? r.text() : ""),
                    fetch('/lgstatus.html').then(r => r.ok ? r.text() : "")
                ]);

                const doc = new DOMParser().parseFromString(`<div>${y}${t}</div>`, 'text/html');
                const rows = doc.querySelectorAll('tr');
                let fragment = document.createDocumentFragment();

                rows.forEach(row => {
                    const td = row.querySelectorAll('td');
                    // Target exactly 9 columns:
                    // [0]Time [1]Mode [2]CP [3]In/Out [4]Tgt [5]DHW/Tgt [6]Air [7]Flags [8]Err
                    if (td.length === 9) {
                        const r = document.createElement('div');
                        r.className = 'log-row';

                        const time = td[0].innerText;
                        const modeTxt = td[1].innerText;
                        const modeBg  = td[1].style.backgroundColor;
                        const cpState = td[2].innerText.trim();
                        const ioTemps = td[3].innerText;
                        const hTgt    = td[4].innerText;
                        const dhwData = td[5].innerText;
                        const airTemp = td[6].innerText;
                        const flags   = td[7].innerHTML;
                        const errCode = td[8].innerText.trim();

                        const hasError = errCode !== "0";

                        r.innerHTML = `
                            <div class="time">${time}</div>
                            <div class="mode-cell" style="background:${modeBg || '#777'}">${modeTxt}</div>
                            <div class="cp-col" style="text-align:center; font-weight:bold; color:${cpState==='ON'?'#4CAF50':'#999'}">${cpState}</div>

                            <div><span class="label">Inlet/Outlet</span><span class="val-unit">${ioTemps}</span></div>
                            <div><span class="label">Target</span><span class="val-unit">${hTgt}</span></div>
                            <div><span class="label">DHW/Target</span><span class="val-unit">${dhwData}</span></div>
                            <div class="outdoor-col"><span class="label">Outdoor</span><span class="val-unit">${airTemp}</span></div>

                            <div class="flags">${flags}</div>
                            <div class="err-cell ${hasError ? 'err-active' : 'err-none'}">${errCode}</div>
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
        setInterval(loadLogs, 30000); // Auto-refresh every 30s
    </script>
</body>
</html>
)raw";

