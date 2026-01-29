const char manager_html[] = R"rawliteral(
<!DOCTYPE HTML>
<html lang="en">
 <head>
  <title>TVMG Manager</title>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  %STYLE%

  <script>
   // Helper to show the filename after user selects a file
   function updateFileName(input, targetId) {
     const fileName = input.files.length ? input.files[0].name : "Choose File";
     document.getElementById(targetId).innerText = fileName;
   }

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
    const select = document.getElementById('delete_path');
    const filename = select.value;
    if (filename === "choose" || filename === "") { alert("Please select a file to delete."); return false; }
    return confirm("Are you sure you want to permanently delete: " + filename + "?");
   }
  </script>
 </head>

 <body>
  <div class="container">
   <h2>ThermaV Monitor</h2>

   <fieldset>
    <legend>Gadget Status</legend>
    <table>
     <tr><td colspan="2">Version: %VERSION%</td><td colspan="2">%UPTIME%</td></tr>
     <tr>
      <td width="25%%">%IPADDR%</td><td width="25%%">%WIFI%</td>
      <td width="25%%">%MODBUS%</td><td width="25%%">%EMON%</td>
     </tr>
    </table>
    <div class="form-row" style="border:none; padding-top:8px; justify-content: flex-start;">
       <a href="/history" class="nav-btn">View Event Log &rarr;</a>
    </div>
    <div id="ota_form" class="form-row" style="border:none; padding-top:8px;">
      <span class="form-label" style="min-width:auto; color: #4CAF50; font-weight: bold;">Firmware Update</span>
      <form style="justify-content: space-between;">
        <label for="update" class="file-input-label" id="label_update">Choose .bin</label>
        <input type="file" id="update" name="update" accept=".bin" onchange="updateFileName(this, 'label_update')">
        <button type="button" id="update-btn" onclick="startOTAUpdate()">Update!</button>
      </form>
    </div>
    <div id="ota_progress_ui" style="display:none;">
     <p id="ota_status" style="text-align:center;">Flashing...</p>
     <div class="progress-wrapper" style="display:block;"><div id="ota_bar" class="progress-bar">0%%</div></div>
    </div>
   </fieldset>

   <fieldset>
    <legend>Filesystem (%FS_USED_BYTES% / %FS_TOTAL_BYTES%)</legend>
    <div style="max-height: 120px; overflow-y: auto; background: #fafafa; border: 1px solid #eee; padding: 4px; border-radius: 3px;">
      %LISTEN_FILES%
    </div>
   </fieldset>

   <fieldset>
    <legend>File Actions</legend>
    <div class="form-row">
        <span class="form-label">Upload</span>
        <form method="POST" action="/upload" enctype="multipart/form-data">
            <label for="upload_data" class="file-input-label" id="label_upload">Choose File</label>
            <input type="file" id="upload_data" name="upload_data" onchange="updateFileName(this, 'label_upload')">
            <input type="submit" value="Upload" onclick="if(!document.getElementById('upload_data').files.length) return false;">
        </form>
    </div>
    <div class="form-row"><span class="form-label">Edit</span><form method="GET" action="/edit">%EDIT_FILES% <input type="submit" value="Edit"></form></div>
    <div class="form-row"><span class="form-label">Delete</span><form method="GET" action="/delete">%DELETE_FILES% <input type="submit" value="Delete" onclick="return confirmFileDelete()"></form></div>
    <div class="form-row"><span class="form-label">Download</span><form method="GET" action="/download">%DOWNLOAD_FILES% <input type="submit" value="Get"></form></div>
   </fieldset>

   <fieldset>
    <legend>Gadget Controls</legend>
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
