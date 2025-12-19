const char manager_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML>
<html lang="en">
 <head>
  <title>TMVG Manager</title>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
   * { box-sizing: border-box; }
   body { background-color: #f7f7f7; font-family: system-ui, -apple-system, sans-serif; font-size: 13px; line-height: 1.2; color: #333; margin: 0; padding: 10px; }

   .container { max-width: 680px; margin: 0 auto; }
   h2 { margin: 6px 0 2px 0; font-size: 1.2em; text-align: center; }

   fieldset { background-color: #fff; border: 1px solid #ccc; border-radius: 4px; margin-bottom: 6px; padding: 8px 12px; width: 100%%; }
   legend { font-weight: bold; padding: 0 4px; font-size: 0.85em; color: #666; }

   table { border-collapse: collapse; width: 100%%; margin-bottom: 2px; font-size: 0.9em; }
   td, th { border: 1px solid #eee; padding: 4px 8px; text-align: left; }
   tr:nth-child(even) { background-color: #fcfcfc; }

   .form-row { display: flex; justify-content: space-between; align-items: center; padding: 4px 0; gap: 15px; border-bottom: 1px solid #f0f0f0; min-height: 32px; }
   .form-row:last-child { border-bottom: none; }
   .form-label { flex: 0 0 auto; min-width: 120px; }

   /* Form Fix: Added gap and flex-grow for filename space */
   form { margin: 0; display: inline-flex; align-items: center; gap: 12px; flex: 1; justify-content: flex-end; }

   input[type="submit"], input[type="button"], button {
    padding: 0 10px; height: 24px; cursor: pointer; border-radius: 3px; border: 1px solid #bbb;
    background: #f0f0f0; font-size: 11px; font-weight: 500; white-space: nowrap;
   }
   input[type="submit"]:hover { background: #e5e5e5; }

   /* File input sizing to prevent overlap */
   input[type="file"] { font-size: 12px; flex: 1; min-width: 0; }
   select { font-size: 12px; max-width: 200px; }

   input[type="checkbox"] { width: 17px; height: 17px; cursor: pointer; margin: 0; }
   #reset_notice { color: #d32f2f; font-weight: bold; }

   .progress-wrapper { width: 100%%; background-color: #eee; border-radius: 4px; margin: 6px 0; display: none; overflow: hidden; border: 1px solid #ddd; }
   .progress-bar { width: 0%%; height: 16px; background-color: #4CAF50; text-align: center; color: white; line-height: 16px; transition: width 0.3s ease; font-size: 10px; }

   .spinner { border: 2px solid #f3f3f3; border-top: 2px solid #3498db; border-radius: 50%%; width: 12px; height: 12px; animation: spin 1s linear infinite; display: inline-block; vertical-align: middle; margin-left: 5px; }
   @keyframes spin { 0%% { transform: rotate(0deg); } 100%% { transform: rotate(360deg); } }
  </style>

  <script>
   function startOTAUpdate() {
    const input = document.getElementById('update');
    if(!input.files.length) { alert("Select file"); return; }
    document.getElementById('ota_form').style.display = 'none';
    document.getElementById('ota_progress_ui').style.display = 'block';
    const bar = document.getElementById('ota_bar');
    const status = document.getElementById('ota_status');
    const source = new EventSource('/events');
    source.addEventListener('ota_progress', function(e) {
     const progress = parseInt(e.data);
     if(!isNaN(progress)) { bar.style.width = progress + '%%'; bar.innerHTML = progress + '%%'; }
    });
    source.addEventListener('ota_state', function(e) {
     if (e.data === "reboot") {
      source.close();
      status.innerHTML = "<b>Rebooting...</b> <div class='spinner'></div>";
      setTimeout(() => { window.location.href = "/manager?v=" + Math.random(); }, 10000);
     } else if (e.data.startsWith("failed")) {
      source.close();
      alert("Error: " + e.data.split(":")[1]);
      location.reload();
     }
    });
    const xhr = new XMLHttpRequest();
    const formData = new FormData();
    formData.append("update", input.files[0]);
    xhr.open("POST", "/update", true);
    xhr.send(formData);
   }

   function checkbox(el) {
    const xhr = new XMLHttpRequest();
    xhr.open("GET", "/checkbox?item=" + el.id + "&state=" + (el.checked ? "1" : "0"), true);
    xhr.send();
   }

   function confirmFileDelete() {
    // Look for the select element inside the delete row
    const select = document.getElementById('delete_path');
    const filename = select.value;

    if (filename === "choose" || filename === "") {
        alert("Please select a file to delete.");
        return false;
    }

    // This puts the filename directly into the browser's confirm dialog
    return confirm("Are you sure you want to permanently delete: " + filename + "?");
   }
  </script>
 </head>

 <body>
  <div class="container">
   <h2>ThermaV Monitor</h2>

   <fieldset>
    <legend>System Status</legend>
    <table>
     <tr><td colspan="2">Version: %VERSION%</td><td colspan="2">%UPTIME%</td></tr>
     <tr>
      <td width="25%%">%IPADDR%</td><td width="25%%">%WIFI%</td>
      <td width="25%%">%MODBUS%</td><td width="25%%">%EMON%</td>
     </tr>
    </table>
    <div id="ota_form" class="form-row" style="border:none; padding-top:8px;">
      <span class="form-label" style="min-width:auto; color: #4CAF50; font-weight: bold;">Firmware Update</span>
      <form style="justify-content: space-between;">
        <input type="file" id="update" name="update" accept=".bin">
        <button type="button" onclick="startOTAUpdate()">Update!</button>
      </form>
    </div>
    <div id="ota_progress_ui" style="display:none;">
     <p id="ota_status" style="text-align:center;">Flashing...</p>
     <div class="progress-wrapper" style="display:block;"><div id="ota_bar" class="progress-bar">0%%</div></div>
    </div>
   </fieldset>

   <fieldset>
    <legend>Filesystem (%SPIFFS_USED_BYTES% / %SPIFFS_TOTAL_BYTES%)</legend>
    <div style="max-height: 120px; overflow-y: auto; background: #fafafa; border: 1px solid #eee; padding: 4px; border-radius: 3px;">
      %LISTEN_FILES%
    </div>
   </fieldset>

   <fieldset>
    <legend>File Actions</legend>
    <div class="form-row"><span class="form-label">Upload</span><form method="POST" action="/upload" enctype="multipart/form-data"><input type="file" id="upload_data" name="upload_data"><input type="submit" value="Upload" onclick="if(!document.getElementById('upload_data').files.length) return false;"></form></div>
    <div class="form-row"><span class="form-label">Edit</span><form method="GET" action="/edit">%EDIT_FILES% <input type="submit" value="Edit"></form></div>
    <div class="form-row"><span class="form-label">Delete</span><form method="GET" action="/delete">%DELETE_FILES% <input type="submit" value="Delete" onclick="return confirmFileDelete()"></form></div>
    <div class="form-row"><span class="form-label">Download</span><form method="GET" action="/download">%DOWNLOAD_FILES% <input type="submit" value="Get"></form></div>
   </fieldset>

   <fieldset>
    <legend>System Controls</legend>
    <div class="form-row">
     <span class="form-label">Restart Device</span>
     <form method="POST" action="/reboot"><input type="submit" value="Restart"></form>
    </div>
    %OPTIONS_SECTION%
    <div class="form-row">
     <span id="reset_notice" class="form-label">Factory Reset (requires confirmation)</span>
     <form method="POST" action="/reset"><input type="submit" value="Reset" onclick="return confirm('WARNING: Pressing OK will immediately reset to defaults and restart')"></form>
    </div>
   </fieldset>

  </div>
 </body>
</html>
)rawliteral";
